#include "../firmware/flexispot_zigbee/status_led_output.h"
#include <array>
#include <cassert>
#include <iostream>
using namespace desk;

bool initOk = true, txOk = true, completed = true;
unsigned initCalls = 0, writes = 0;
rmt_data_t *payload = nullptr;
size_t count = 0;
bool rmtInit(int, int, int, uint32_t hz) {
  assert(hz == 10000000); ++initCalls; return initOk;
}
bool rmtTransmitCompleted(int) { return completed; }
bool rmtWriteAsync(int, rmt_data_t *data, size_t size) {
  ++writes; payload = data; count = size; completed = false; return txOk;
}
std::array<uint8_t, 3> decode() {
  assert(count == 25);
  std::array<uint8_t, 3> bytes{};
  for (unsigned i = 0; i < 24; ++i) {
    assert(payload[i].level0 == 1 && payload[i].level1 == 0);
    assert(payload[i].duration0 + payload[i].duration1 == 12);
    assert(payload[i].duration0 == 4 || payload[i].duration0 == 8);
    bytes[i / 8] = uint8_t((bytes[i / 8] << 1) | (payload[i].duration0 == 8));
  }
  assert(!payload[24].level0 && !payload[24].level1);
  assert(payload[24].duration0 + payload[24].duration1 == 4000);
  return bytes;
}
int main() {
  StatusLedOutput disabled(-1, LED_COLOR_ORDER_GRB, 255);
  disabled.begin(); disabled.show({1,2,3}, 0);
  assert(!disabled.failed() && initCalls == 0 && writes == 0);

  StatusLedOutput led(8, LED_COLOR_ORDER_GRB, 255);
  led.begin(); led.begin(); assert(initCalls == 1);
  led.show({0x12,0x34,0x56}, 0);
  assert((decode() == std::array<uint8_t,3>{{0x34,0x12,0x56}}));
  auto *original = payload;
  // A pending asynchronous transfer must retain unmodified, valid payload storage.
  led.show({0xff,0,0}, 10);
  assert(writes == 1 && original == payload);
  assert((decode() == std::array<uint8_t,3>{{0x34,0x12,0x56}}));
  completed = true;
  led.show({0xff,0,0}, 11);
  assert(writes == 2 && original == payload);
  assert((decode() == std::array<uint8_t,3>{{0,0xff,0}}));
  led.show({1,2,3}, 61); // Missing completion callback disables only the LED.
  assert(led.failed());
  led.show({4,5,6}, 1000000);
  assert(writes == 2);
  assert((decode() == std::array<uint8_t,3>{{0,0xff,0}})); // Still alive and untouched.

  const rgb_led_color_order_t orders[] = {LED_COLOR_ORDER_RGB, LED_COLOR_ORDER_BGR,
    LED_COLOR_ORDER_BRG, LED_COLOR_ORDER_RBG, LED_COLOR_ORDER_GBR, LED_COLOR_ORDER_GRB};
  const std::array<uint8_t,3> expected[] = {{{0x12,0x34,0x56}}, {{0x56,0x34,0x12}},
    {{0x56,0x12,0x34}}, {{0x12,0x56,0x34}}, {{0x34,0x56,0x12}}, {{0x34,0x12,0x56}}};
  for (unsigned i = 0; i < 6; ++i) {
    completed = true;
    StatusLedOutput pixel(8, orders[i], 255);
    pixel.begin(); pixel.show({0x12,0x34,0x56}, 0);
    assert(decode() == expected[i]);
    completed = true; // Complete transfer before the test object leaves scope.
  }
  StatusLedOutput dim(8, LED_COLOR_ORDER_RGB, 24);
  dim.begin(); dim.show({255,64,0}, 0);
  assert((decode() == std::array<uint8_t,3>{{24,6,0}}));
  completed = true;

  StatusLedOutput wrap(8, LED_COLOR_ORDER_RGB, 255);
  wrap.begin(); wrap.show({1,2,3}, UINT32_MAX - 20);
  wrap.show({4,5,6}, 29);
  assert(wrap.failed());
  completed = true;

  initOk = false;
  StatusLedOutput initFail(8, LED_COLOR_ORDER_RGB, 255);
  const unsigned before = writes;
  initFail.begin(); initFail.show({1,2,3}, 0);
  assert(initFail.failed() && writes == before);
  initOk = true; txOk = false;
  StatusLedOutput txFail(8, LED_COLOR_ORDER_RGB, 255);
  txFail.begin(); txFail.show({1,2,3}, 0);
  assert(txFail.failed());
  std::cout << "Asynchronous LED lifetime, stall, wire order, and failure tests passed\n";
}
