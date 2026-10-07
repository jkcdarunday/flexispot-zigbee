#include <Arduino.h>
#include <Zigbee.h>
#include <driver/gpio.h>
#include "config.h"
#include "desk_protocol.h"
#include "height_endpoint.h"

#if !defined(CONFIG_IDF_TARGET_ESP32H2)
#error "Select ESP32H2 Dev Module"
#endif
#ifndef ZIGBEE_MODE_ED
#error "Select Zigbee ED mode and the Zigbee partition scheme"
#endif

// Always-awake end device: mains powered, without acting as a mesh router.
HardwareSerial deskSerial(1);
desk::Parser parser;
desk::Controller controller(DESK_NUDGE_MS, DESK_PRESET_HOLD_MS);
HeightEndpoint heightSensor(9);
ZigbeeLight standControl(1), sitControl(2), preset1Control(3), preset2Control(4);
ZigbeeLight upControl(5), downControl(6), memoryControl(7), releaseControl(8);
ZigbeeLight *controls[] = {&standControl, &sitControl, &preset1Control, &preset2Control,
                          &upControl, &downControl, &memoryControl, &releaseControl};

// Zigbee callbacks run in another task. They only enqueue; UART belongs to loop().
QueueHandle_t requests;
portMUX_TYPE requestMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t acknowledgements = 0;
bool releaseRequested = false;

template<unsigned Index> void onControl(bool on) {
  if (!on) return;  // includes our automatic reset to OFF
  portENTER_CRITICAL(&requestMux);
  acknowledgements |= uint8_t(1u << Index);
  if (Index == 7) releaseRequested = true;  // never blocked by a full queue
  portEXIT_CRITICAL(&requestMux);
  if (Index != 7) {
    const uint8_t command = Index;
    xQueueSend(requests, &command, 0);  // drop excess commands, never block Zigbee
  }
}

void sendDesk(desk::Command command) {
  uint8_t packet[8];
  desk::commandPacket(command, packet);
  deskSerial.write(packet, sizeof(packet));
#if DESK_DEBUG_UART
  Serial.print("TX:");
  for (uint8_t b : packet) Serial.printf(" %02X", b);
  Serial.println();
#endif
}

void reportOff(uint8_t endpoint) {
  // setLight(false) has already updated the value. Explicit report for momentary UI.
  esp_zb_zcl_report_attr_cmd_t report = {};
  report.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
  report.zcl_basic_cmd.src_endpoint = endpoint;
  report.clusterID = ESP_ZB_ZCL_CLUSTER_ID_ON_OFF;
  report.attributeID = ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID;
  report.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
  report.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;
  esp_zb_lock_acquire(portMAX_DELAY);
  esp_zb_zcl_report_attr_cmd_req(&report);
  esp_zb_lock_release();
}

float latestHeightCm = NAN;
float reportedHeightCm = NAN;
uint32_t lastHeightReport = 0;
uint32_t lastIdlePacket = 0;
bool wasConnected = false;

void setup() {
  Serial.begin(115200);  // native USB CDC; does not use desk UART pins
  if (!GPIO_IS_VALID_OUTPUT_GPIO(DESK_TX_PIN) || !GPIO_IS_VALID_GPIO(DESK_RX_PIN) ||
      !GPIO_IS_VALID_OUTPUT_GPIO(DESK_WAKE_PIN) ||
      (RESET_BUTTON_PIN >= 0 && !GPIO_IS_VALID_GPIO(RESET_BUTTON_PIN))) {
    Serial.println("Invalid GPIO configuration. Desk interface disabled.");
    while (true) delay(1000);
  }
  requests = xQueueCreate(8, sizeof(uint8_t));
  if (!requests) abort();
  pinMode(DESK_WAKE_PIN, OUTPUT);
  digitalWrite(DESK_WAKE_PIN, HIGH);
  if (RESET_BUTTON_PIN >= 0) pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  deskSerial.begin(9600, SERIAL_8N1, DESK_RX_PIN, DESK_TX_PIN);
  controller.begin(millis());
  standControl.onLightChange(onControl<0>);
  sitControl.onLightChange(onControl<1>);
  preset1Control.onLightChange(onControl<2>);
  preset2Control.onLightChange(onControl<3>);
  upControl.onLightChange(onControl<4>);
  downControl.onLightChange(onControl<5>);
  memoryControl.onLightChange(onControl<6>);
  releaseControl.onLightChange(onControl<7>);
  for (auto *control : controls) {
    control->setManufacturerAndModel("JKCD", "Flexispot-E7Q-H2");
    control->setPowerSource(ZB_POWER_SOURCE_MAINS);
    Zigbee.addEndpoint(control);
  }
  heightSensor.setManufacturerAndModel("JKCD", "Flexispot-E7Q-H2");
  heightSensor.setPowerSource(ZB_POWER_SOURCE_MAINS);
  Zigbee.addEndpoint(&heightSensor);
  Zigbee.setRxOnWhenIdle(true);
  if (!Zigbee.begin()) {
    Serial.println("Zigbee startup failed; restarting.");
    ESP.restart();
  }
  Serial.printf("Desk UART TX=%d RX=%d WAKE=%d; permit joining on your coordinator.\n",
                DESK_TX_PIN, DESK_RX_PIN, DESK_WAKE_PIN);
  // No wait for pairing: serial service and reset button must keep working offline.
}

void loop() {
  const uint32_t now = millis();
  const bool connected = Zigbee.connected();
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
  }
  const auto previousState = controller.state();
  controller.tick(now);  // deadlines checked before processing incoming polls
  if (previousState == desk::Controller::State::Active &&
      controller.state() == desk::Controller::State::Idle)
    sendDesk(desk::Command::Release);
  uint8_t command;
  while (xQueueReceive(requests, &command, 0) == pdTRUE) {
    if (connected && !release) {
      const bool accepted = controller.request(static_cast<desk::Command>(command), now);
      Serial.printf("Command %u: %s\n", command + 1, accepted ? "accepted" : "busy/booting; dropped");
    }
  }
  digitalWrite(DESK_WAKE_PIN, controller.wakeHigh() ? HIGH : LOW);
  // Limit serial work per iteration so noise cannot starve deadlines or reset handling.
  for (unsigned i = 0; i < 128 && deskSerial.available(); ++i) {
    const uint8_t byte = deskSerial.read();
#if DESK_DEBUG_UART
    Serial.printf("RX: %02X\n", byte);
#endif
    desk::Event event;
    if (!parser.feed(byte, now, event)) continue;
    if (event.poll) sendDesk(controller.pollResponse());
    if (event.hasHeight) {
      latestHeightCm = event.height * (DESK_DISPLAY_IN_INCHES ? 2.54f : 1.0f);
    }
  }
  if (controller.state() == desk::Controller::State::Idle && now - lastIdlePacket >= 3000) {
    sendDesk(desk::Command::Release);
    lastIdlePacket = now;
  }
  for (unsigned i = 0; i < 8; ++i) if (acks & (1u << i)) {
    controls[i]->setLight(false);
    if (connected) reportOff(i + 1);
  }
  if (connected && !isnan(latestHeightCm) &&
      ((!wasConnected) || now - lastHeightReport >= 60000 ||
       (latestHeightCm != reportedHeightCm && now - lastHeightReport >= 1000))) {
    if (heightSensor.setHeight(latestHeightCm) && heightSensor.reportHeight()) {
      reportedHeightCm = latestHeightCm;
      lastHeightReport = now;
    }
  }
  wasConnected = connected;

  static bool resetHeld = false;
  static uint32_t resetSince = 0;
  if (RESET_BUTTON_PIN >= 0 && digitalRead(RESET_BUTTON_PIN) == LOW) {
    if (!resetHeld) { resetHeld = true; resetSince = now; }
    if (now - resetSince >= 5000) {
      controller.release();
      digitalWrite(DESK_WAKE_PIN, HIGH);
      sendDesk(desk::Command::Release);
      Serial.println("Clearing Zigbee pairing and restarting.");
      Zigbee.factoryReset();
    }
  } else resetHeld = false;
  delay(1);
}
