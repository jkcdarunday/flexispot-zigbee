#pragma once
#include <Zigbee.h>
#include "zigbee_access.h"

// Reuse the On/Off implementation, but advertise a general actuator, not a
// light. ZHA discovers device type 0x0002 as a switch without a custom handler.
class DeskControl : public ZigbeeLight {
 public:
  explicit DeskControl(uint8_t endpoint) : ZigbeeLight(endpoint) {
    _device_id = ESP_ZB_HA_ON_OFF_OUTPUT_DEVICE_ID;
    _ep_config.app_device_id = ESP_ZB_HA_ON_OFF_OUTPUT_DEVICE_ID;
  }
  void onPress(void (*callback)(bool)) { onLightChange(callback); }
  bool clearPress() {
    // setLight() waits forever for the lock. Reset the ZCL attribute directly;
    // the inherited callback reads each new incoming value independently.
    if (!acquireDeskZigbeeLock()) return false;
    bool off = false;
    const auto status = esp_zb_zcl_set_attribute_val(_endpoint,
        ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &off, false);
    esp_zb_lock_release();
    return status == ESP_ZB_ZCL_STATUS_SUCCESS;
  }
};
