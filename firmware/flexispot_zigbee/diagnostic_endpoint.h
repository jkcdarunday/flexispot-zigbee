#pragma once
#include <Zigbee.h>
#include "zigbee_access.h"

// Keep ten application endpoints: the pinned ZBOSS reporting slot layout
// assumes at most ten. Standard Analog Input exposes uptime without a quirk;
// the two read-only extensions provide exact boot count and reset cause.
class DiagnosticEndpoint : public ZigbeeEP {
 public:
  explicit DiagnosticEndpoint(uint8_t endpoint) : ZigbeeEP(endpoint) {
    _device_id = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID;
    _ep_config = {.endpoint = endpoint, .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
                  .app_device_id = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID, .app_device_version = 0};
    _cluster_list = esp_zb_zcl_cluster_list_create();
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster(_cluster_list,
        esp_zb_basic_cluster_create(nullptr), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(_cluster_list,
        esp_zb_identify_cluster_create(nullptr), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    esp_zb_analog_input_cluster_cfg_t cfg = {};
    cfg.present_value = NAN;
    auto *cluster = esp_zb_analog_input_cluster_create(&cfg);
    ESP_ERROR_CHECK(esp_zb_analog_input_cluster_add_attr(cluster,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_DESCRIPTION_ID, description_));
    ESP_ERROR_CHECK(esp_zb_analog_input_cluster_add_attr(cluster,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_ENGINEERING_UNITS_ID, &unit_));
    ESP_ERROR_CHECK(esp_zb_analog_input_cluster_add_attr(cluster,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_RESOLUTION_ID, &resolution_));
    constexpr uint8_t access = ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING;
    ESP_ERROR_CHECK(esp_zb_cluster_add_attr(cluster, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        0xf000, ESP_ZB_ZCL_ATTR_TYPE_U32, access, &bootCount_));
    ESP_ERROR_CHECK(esp_zb_cluster_add_attr(cluster, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        0xf001, ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM, access, &resetReason_));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_analog_input_cluster(_cluster_list, cluster,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
  }
  bool updateUptime(float value, bool report) {
    return update(ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID, &value, report);
  }
  bool updateBootCount(uint32_t value, bool report) { return update(0xf000, &value, report); }
  bool updateResetReason(uint8_t value, bool report) { return update(0xf001, &value, report); }
 private:
  bool update(uint16_t attribute, void *value, bool report) {
    if (!acquireDeskZigbeeLock()) return false;
    const auto status = esp_zb_zcl_set_attribute_val(_endpoint,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        attribute, value, false);
    esp_err_t result = ESP_OK;
    if (status == ESP_ZB_ZCL_STATUS_SUCCESS && report) {
      esp_zb_zcl_report_attr_cmd_t command = {};
      command.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
      command.zcl_basic_cmd.src_endpoint = _endpoint;
      command.clusterID = ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT;
      command.attributeID = attribute;
      command.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
      command.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;
      result = esp_zb_zcl_report_attr_cmd_req(&command);
    }
    esp_zb_lock_release();
    return status == ESP_ZB_ZCL_STATUS_SUCCESS && result == ESP_OK;
  }
  char description_[8] = "\x06" "Uptime";
  uint16_t unit_ = 73;  // BACnet seconds; no temperature application_type
  float resolution_ = 1;
  uint32_t bootCount_ = 0;  // zero means the NVS counter is unavailable
  uint8_t resetReason_ = 0;
};
