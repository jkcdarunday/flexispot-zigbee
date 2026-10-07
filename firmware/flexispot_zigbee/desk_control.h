#pragma once
#include <Zigbee.h>

// Reuse the On/Off implementation, but advertise a general actuator, not a
// light. ZHA discovers device type 0x0002 as a switch without a custom handler.
class DeskControl : public ZigbeeLight {
 public:
  explicit DeskControl(uint8_t endpoint) : ZigbeeLight(endpoint) {
    _device_id = ESP_ZB_HA_ON_OFF_OUTPUT_DEVICE_ID;
    _ep_config.app_device_id = ESP_ZB_HA_ON_OFF_OUTPUT_DEVICE_ID;
  }
  void onPress(void (*callback)(bool)) { onLightChange(callback); }
  bool clearPress() { return setLight(false); }
};
