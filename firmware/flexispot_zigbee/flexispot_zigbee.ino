#include <Arduino.h>
#include <Zigbee.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <stdarg.h>
#if CONFIG_PM_ENABLE
#include <esp_pm.h>
#endif
#include "config.h"
#include "desk_protocol.h"
#include "height_endpoint.h"
#include "desk_control.h"
#include "status_indicator.h"
#include "connection_health.h"
#include "zigbee_access.h"
#include "desk_transmitter.h"
#include "status_led_output.h"

#if !defined(CONFIG_IDF_TARGET_ESP32H2)
#error "Select ESP32H2 Dev Module"
#endif
#ifndef ZIGBEE_MODE_ED
#error "Select Zigbee ED mode and the Zigbee partition scheme"
#endif

// Always-awake end device: mains powered, without acting as a mesh router.
HardwareSerial deskSerial(1);
struct DeskUartDriver {
  uint32_t now() const { return millis(); }
  bool ready() {
    return deskSerial && uart_wait_tx_done(UART_NUM_1, 0) == ESP_OK;
  }
  size_t write(const uint8_t *data, size_t size) {
    const int written = uart_tx_chars(UART_NUM_1, reinterpret_cast<const char *>(data), size);
    return written > 0 ? static_cast<size_t>(written) : 0;
  }
} deskUartDriver;
desk::DeskTransmitter<DeskUartDriver> deskTransmitter(deskUartDriver);
desk::StatusLedOutput statusLed(STATUS_LED_PIN, STATUS_LED_COLOR_ORDER, STATUS_LED_BRIGHTNESS);
desk::Parser parser;
desk::Controller controller(DESK_NUDGE_MS, DESK_PRESET_HOLD_MS);
desk::StatusIndicator statusIndicator;
HeightEndpoint heightSensor(9);
DeskControl standControl(1), sitControl(2), preset1Control(3), preset2Control(4);
DeskControl upControl(5), downControl(6), memoryControl(7), releaseControl(8);
DeskControl *controls[] = {&standControl, &sitControl, &preset1Control, &preset2Control,
                          &upControl, &downControl, &memoryControl, &releaseControl};

// Zigbee callbacks run in another task. They only enqueue; UART belongs to loop().
QueueHandle_t requests;
portMUX_TYPE requestMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t acknowledgements = 0;
bool releaseRequested = false;
constexpr unsigned requestDepth = 8;

// No heap allocation, waiting for a USB host, or filling a disconnected console.
void logMessage(const char *format, ...) __attribute__((format(printf, 1, 2)));
void logMessage(const char *format, ...) {
  if (!Serial) return;
  char text[256];
  va_list args;
  va_start(args, format);
  const int count = vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  if (count <= 0) return;
  const size_t size = static_cast<size_t>(count) < sizeof(text) ? static_cast<size_t>(count) : sizeof(text) - 1;
  if (Serial.availableForWrite() >= static_cast<int>(size))
    Serial.write(reinterpret_cast<const uint8_t *>(text), size);
}

const char *resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external-reset";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt-watchdog";
    case ESP_RST_TASK_WDT: return "task-watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    default: return "other";
  }
}

void configureLoopWatchdog(uint32_t timeoutMs) {
  esp_task_wdt_config_t watchdog = {};
  watchdog.timeout_ms = timeoutMs;
  watchdog.idle_core_mask = 0;
  watchdog.trigger_panic = true;
  esp_err_t result = esp_task_wdt_init(&watchdog);
  if (result == ESP_ERR_INVALID_STATE) result = esp_task_wdt_reconfigure(&watchdog);
  ESP_ERROR_CHECK(result);
}

bool powerPolicyConfirmed = false;
void enforceAwakePolicy(uint32_t now) {
  if (!Zigbee.started()) return;
  static bool attempted = false;
  static uint32_t lastAttempt = 0;
  if (attempted && now - lastAttempt < (powerPolicyConfirmed ? 60000u : 1000u)) return;
  attempted = true;
  lastAttempt = now;
  if (!acquireDeskZigbeeLock()) return;
  // Arduino 3.3.6's helper alone does not apply this to mains-powered endpoints.
  esp_zb_sleep_enable(false);
  esp_zb_set_rx_on_when_idle(true);
  const bool rxOn = esp_zb_get_rx_on_when_idle();
  const bool sleeping = esp_zb_sleep_is_enable();
  esp_zb_lock_release();
  const bool wasConfirmed = powerPolicyConfirmed;
  powerPolicyConfirmed = rxOn && !sleeping;
  if (!wasConfirmed || !powerPolicyConfirmed)
    logMessage("Zigbee power: rx_idle=%u sleep=%u verified=%u\n",
               unsigned(rxOn), unsigned(sleeping), unsigned(powerPolicyConfirmed));
}

void updateStatusLed(bool connected, bool fault = false) {
  if (STATUS_LED_PIN < 0) return;
  const auto color = statusIndicator.color(millis(), connected, fault);
  statusLed.show(color, millis());
}

template<unsigned Index> void onControl(bool on) {
  if (!on) return;  // includes our automatic reset to OFF
  portENTER_CRITICAL(&requestMux);
  acknowledgements |= uint8_t(1u << Index);
  if (Index == 7) releaseRequested = true;  // never blocked by a full queue
  portEXIT_CRITICAL(&requestMux);
  if (Index != 7) {
    const desk::CommandRequest request{static_cast<desk::Command>(Index), millis()};
    xQueueSend(requests, &request, 0);  // drop excess commands, never block Zigbee
  }
}

bool sendDesk(desk::Command command) {
  const auto previous = deskTransmitter.lastSent();
  const uint32_t errorsBefore = deskTransmitter.errorCount();
  if (!deskTransmitter.send(command)) {
    if (deskTransmitter.errorCount() != errorsBefore) {
      controller.release();
      xQueueReset(requests);
      digitalWrite(DESK_WAKE_PIN, HIGH);
    }
    return false;
  }
  const auto actual = deskTransmitter.lastSent();
  if (actual != desk::Command::Release && actual != previous)
    statusIndicator.commandSent(millis());
#if DESK_DEBUG_UART
  uint8_t packet[8];
  desk::commandPacket(actual, packet);
  logMessage("TX: %02X %02X %02X %02X %02X %02X %02X %02X\n",
             packet[0], packet[1], packet[2], packet[3], packet[4], packet[5], packet[6], packet[7]);
#endif
  return true;
}

void releaseDeskForRestart() {
  controller.release();
  xQueueReset(requests);
  digitalWrite(DESK_WAKE_PIN, HIGH);
  // Drain at most one in-flight frame, then send the retained Release.
  sendDesk(desk::Command::Release);
  for (unsigned i = 0; i < 2 && deskTransmitter.releasePending(); ++i) {
    if (uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(25)) != ESP_OK) break;
    sendDesk(desk::Command::Release);
  }
  uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(25));
}

bool reportOff(uint8_t endpoint) {
  // clearPress() has already updated the value. Explicit report for momentary UI.
  esp_zb_zcl_report_attr_cmd_t report = {};
  report.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
  report.zcl_basic_cmd.src_endpoint = endpoint;
  report.clusterID = ESP_ZB_ZCL_CLUSTER_ID_ON_OFF;
  report.attributeID = ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID;
  report.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
  report.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;
  if (!acquireDeskZigbeeLock()) return false;
  const auto result = esp_zb_zcl_report_attr_cmd_req(&report);
  esp_zb_lock_release();
  return result == ESP_OK;
}

float latestHeightCm = NAN;
float reportedHeightCm = NAN;
uint32_t lastHeightReport = 0;
uint32_t lastIdlePacket = 0;
bool wasConnected = false;
desk::ConnectionHealth connectionHealth;
uint32_t lastHeightAttempt = 0, lastAckAttempt = 0, lastProbe = 0;
bool heightAttempted = false, ackAttempted = false;
uint8_t pendingAcks = 0;
// Callback data is shared with the Zigbee task; protected by requestMux.
bool probePending = false, coordinatorReplied = false;
bool pairingChecked = false;
uint32_t coordinatorReplies = 0, reportQueueFailures = 0, droppedCommands = 0;
uint32_t lastHeightFrame = 0, validUartFrames = 0;
uint32_t lastDiagnostic = 0;

void printDiagnostics(uint32_t now) {
  if (now - lastDiagnostic < 60000) return;
  lastDiagnostic = now;
  logMessage("Health: up_s=%lu reset=%s heap=%u min_heap=%u coord=%lu reply_s=%lu awake=%u retry=%lu dropped=%lu uart=%lu height_s=%lu led_fail=%u tx_err=%lu tx_busy=%lu\n",
      static_cast<unsigned long>(now / 1000), resetReason(), unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()),
      static_cast<unsigned long>(coordinatorReplies),
      static_cast<unsigned long>(connectionHealth.replyAge(now) / 1000), unsigned(powerPolicyConfirmed),
      static_cast<unsigned long>(reportQueueFailures), static_cast<unsigned long>(droppedCommands),
      static_cast<unsigned long>(validUartFrames), static_cast<unsigned long>((now - lastHeightFrame) / 1000),
      unsigned(statusLed.failed()), static_cast<unsigned long>(deskTransmitter.errorCount()),
      static_cast<unsigned long>(deskTransmitter.busyCount()));
}

void coordinatorReply(esp_zb_zdp_status_t status, uint16_t address,
                      esp_zb_af_node_desc_t *, void *) {
  portENTER_CRITICAL(&requestMux);
  probePending = false;
  if (status == ESP_ZB_ZDP_STATUS_SUCCESS && address == 0)
    coordinatorReplied = true;
  portEXIT_CRITICAL(&requestMux);
}

void checkConnection(uint32_t now, bool connected) {
  bool pending, replied;
  portENTER_CRITICAL(&requestMux);
  pending = probePending;
  replied = coordinatorReplied;
  coordinatorReplied = false;
  portEXIT_CRITICAL(&requestMux);
  if (Zigbee.started()) connectionHealth.stackStarted();
  // A saved network also needs recovery when rejoin never completes at boot.
  if (!pairingChecked && Zigbee.started() && acquireDeskZigbeeLock()) {
    if (!esp_zb_bdb_is_factory_new()) connectionHealth.paired(now);
    pairingChecked = true;
    esp_zb_lock_release();
  }
  connectionHealth.observe(now, connected);
  if (replied) {
    connectionHealth.reply(now);
    ++coordinatorReplies;
  }
  // Never accumulate requests when the stack stops invoking callbacks.
  if (connected && !pending && (!wasConnected || now - lastProbe >= 60000)) {
    lastProbe = now;
    if (acquireDeskZigbeeLock()) {
      portENTER_CRITICAL(&requestMux);
      probePending = true;
      portEXIT_CRITICAL(&requestMux);
      esp_zb_zdo_node_desc_req_param_t request = {};
      request.dst_nwk_addr = 0;  // Zigbee coordinator; independent of bindings
      esp_zb_zdo_node_desc_req(&request, coordinatorReply, nullptr);
      esp_zb_lock_release();
    }
  }
  if (connectionHealth.needsRecovery(now)) {
    releaseDeskForRestart();
    logMessage("Zigbee startup or reachability failed for five minutes; restarting with saved pairing.\n");
    ESP.restart();  // Never factory-reset as an automatic recovery.
  }
}


void setup() {
  // Arm before USB, LED, UART or Zigbee initialization can stall.
  configureLoopWatchdog(60000);
  enableLoopWDT();
  Serial.begin(115200);  // native USB CDC; does not use desk UART pins
  Serial.setTxTimeoutMs(10);
  logMessage("Boot: reset=%s\n", resetReason());
  if (!GPIO_IS_VALID_OUTPUT_GPIO(DESK_TX_PIN) || !GPIO_IS_VALID_GPIO(DESK_RX_PIN) ||
      !GPIO_IS_VALID_OUTPUT_GPIO(DESK_WAKE_PIN) ||
      (RESET_BUTTON_PIN >= 0 && !GPIO_IS_VALID_GPIO(RESET_BUTTON_PIN)) ||
      (STATUS_LED_PIN >= 0 && !GPIO_IS_VALID_OUTPUT_GPIO(STATUS_LED_PIN))) {
    logMessage("Invalid GPIO configuration. Desk interface disabled.\n");
    if (STATUS_LED_PIN >= 0 && GPIO_IS_VALID_OUTPUT_GPIO(STATUS_LED_PIN)) {
      statusLed.begin();
      updateStatusLed(false, true);
    }
    while (true) { esp_task_wdt_reset(); delay(1000); }
  }
  statusLed.begin();
  updateStatusLed(false);
  requests = xQueueCreate(requestDepth, sizeof(desk::CommandRequest));
  if (!requests) abort();
  pinMode(DESK_WAKE_PIN, OUTPUT);
  digitalWrite(DESK_WAKE_PIN, HIGH);
  if (RESET_BUTTON_PIN >= 0) pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  // uart_tx_chars() is nonblocking and requires no software TX ring buffer.
  deskSerial.setTxBufferSize(0);
  deskSerial.begin(9600, SERIAL_8N1, DESK_RX_PIN, DESK_TX_PIN);
  if (!deskSerial) {
    logMessage("Desk UART initialization failed.\n");
    abort();
  }
  controller.begin(millis());
  standControl.onPress(onControl<0>);
  sitControl.onPress(onControl<1>);
  preset1Control.onPress(onControl<2>);
  preset2Control.onPress(onControl<3>);
  upControl.onPress(onControl<4>);
  downControl.onPress(onControl<5>);
  memoryControl.onPress(onControl<6>);
  releaseControl.onPress(onControl<7>);
  for (auto *control : controls) {
    control->setManufacturerAndModel("JKCD", "Flexispot-E7Q-H2");
    control->setPowerSource(ZB_POWER_SOURCE_MAINS);
    Zigbee.addEndpoint(control);
  }
  heightSensor.setManufacturerAndModel("JKCD", "Flexispot-E7Q-H2");
  heightSensor.setPowerSource(ZB_POWER_SOURCE_MAINS);
  Zigbee.addEndpoint(&heightSensor);
  // Keep CPU light sleep disabled too, if a future core enables power management.
#if CONFIG_PM_ENABLE
  static esp_pm_lock_handle_t noLightSleep;
  ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "desk-awake", &noLightSleep));
  ESP_ERROR_CHECK(esp_pm_lock_acquire(noLightSleep));
#endif
  connectionHealth.begin(millis());
  Zigbee.setRxOnWhenIdle(true);
  if (!Zigbee.begin()) {
    // begin() can time out while the stack is still retrying initialization
    // or a saved-network rejoin. Keep UART, LED and pairing reset responsive.
    logMessage("Zigbee startup pending; keeping desk interface active while the stack retries.\n");
  }
  logMessage("Desk UART TX=%d RX=%d WAKE=%d; permit joining on your coordinator.\n",
                DESK_TX_PIN, DESK_RX_PIN, DESK_WAKE_PIN);
  logMessage("RGB status LED GPIO=%d brightness=%d\n", STATUS_LED_PIN, STATUS_LED_BRIGHTNESS);
  esp_task_wdt_reset();
  configureLoopWatchdog(30000);
  enforceAwakePolicy(millis());
  // No wait for pairing: serial service and reset button must keep working offline.
}

void loop() {
  const uint32_t now = millis();
  const bool connected = Zigbee.connected();
  if (connected != wasConnected)
    logMessage(connected ? "Zigbee connected.\n" : "Zigbee disconnected.\n");
  bool release;
  uint8_t acks;
  portENTER_CRITICAL(&requestMux);
  release = releaseRequested; releaseRequested = false;
  acks = acknowledgements; acknowledgements = 0;
  portEXIT_CRITICAL(&requestMux);

  if (release || (wasConnected && !connected)) {
    controller.release();
    xQueueReset(requests);
    digitalWrite(DESK_WAKE_PIN, HIGH);
    sendDesk(desk::Command::Release);
    if (release) statusIndicator.commandSent(millis());
  }
  const auto previousState = controller.state();
  controller.tick(millis());  // use fresh time after any earlier logging
  digitalWrite(DESK_WAKE_PIN, controller.wakeHigh() ? HIGH : LOW);
  if (previousState == desk::Controller::State::Active &&
      controller.state() == desk::Controller::State::Idle)
    sendDesk(desk::Command::Release);
  desk::CommandRequest request;
  for (unsigned i = 0; i < requestDepth && xQueueReceive(requests, &request, 0) == pdTRUE; ++i) {
    const bool accepted = connected && !release && controller.request(request, millis());
    if (accepted) digitalWrite(DESK_WAKE_PIN, controller.wakeHigh() ? HIGH : LOW);
    else ++droppedCommands;
    logMessage("Command %u: %s\n", static_cast<unsigned>(request.command) + 1,
               accepted ? "accepted" : "busy/offline/stale; dropped");
  }
  digitalWrite(DESK_WAKE_PIN, controller.wakeHigh() ? HIGH : LOW);
  // Limit serial work per iteration so noise cannot starve deadlines or reset handling.
  for (unsigned i = 0; i < 128 && deskSerial.available(); ++i) {
    const uint8_t byte = deskSerial.read();
#if DESK_DEBUG_UART
    logMessage("RX: %02X\n", byte);
#endif
    desk::Event event;
    if (!parser.feed(byte, now, event)) continue;
    ++validUartFrames;
    if (event.poll) {
      const auto response = controller.pollResponse(millis());
      digitalWrite(DESK_WAKE_PIN, controller.wakeHigh() ? HIGH : LOW);
      sendDesk(response);
    }
    if (event.hasHeight) {
      const float heightCm = event.height * (DESK_DISPLAY_IN_INCHES ? 2.54f : 1.0f);
      if (!isnan(latestHeightCm) && heightCm != latestHeightCm)
        statusIndicator.heightChanged(now);
      latestHeightCm = heightCm;
      lastHeightFrame = now;
    }
  }
  if (controller.state() == desk::Controller::State::Idle && now - lastIdlePacket >= 3000) {
    sendDesk(desk::Command::Release);
    lastIdlePacket = now;
  }
  if (deskTransmitter.releasePending()) sendDesk(desk::Command::Release);
  pendingAcks |= acks;
  if (pendingAcks && (!ackAttempted || now - lastAckAttempt >= 1000)) {
    ackAttempted = true;
    lastAckAttempt = now;
    for (unsigned i = 0; i < 8; ++i) if (pendingAcks & (1u << i)) {
      if (controls[i]->clearPress() && (!connected || reportOff(i + 1)))
        pendingAcks &= uint8_t(~(1u << i));
      else ++reportQueueFailures;
    }
  }
  if (connected && !isnan(latestHeightCm) &&
      (!heightAttempted || now - lastHeightAttempt >= 1000) &&
      ((!wasConnected) || now - lastHeightReport >= 60000 ||
       (latestHeightCm != reportedHeightCm && now - lastHeightReport >= 1000))) {
    heightAttempted = true;
    lastHeightAttempt = now;
    if (heightSensor.setHeight(latestHeightCm) && heightSensor.reportHeight()) {
      reportedHeightCm = latestHeightCm;
      lastHeightReport = now;
    } else ++reportQueueFailures;
  }
  if (deskTransmitter.faulted()) {
    releaseDeskForRestart();
    logMessage("Desk UART stalled or repeatedly failed; restarting with saved pairing.\n");
    ESP.restart();
  }
  enforceAwakePolicy(millis());
  checkConnection(millis(), connected);
  printDiagnostics(millis());
  wasConnected = connected;

  static bool resetHeld = false;
  static uint32_t resetSince = 0;
  if (RESET_BUTTON_PIN >= 0 && digitalRead(RESET_BUTTON_PIN) == LOW) {
    if (!resetHeld) { resetHeld = true; resetSince = now; }
    if (now - resetSince >= 5000) {
      releaseDeskForRestart();
      logMessage("Clearing Zigbee pairing and restarting.\n");
      Zigbee.factoryReset();
    }
  } else resetHeld = false;
  updateStatusLed(connected && connectionHealth.reachable(millis()));
  delay(1);
}
