#pragma once
#include <Zigbee.h>
#include "config.h"
#include "zigbee_access.h"

// Analog Input with engineering_units only: Arduino's ZigbeeAnalog also inserts
// application_type=temperature, which takes precedence over cm in ZHA.
class HeightEndpoint : public ZigbeeEP {
 public:
  explicit HeightEndpoint(uint8_t endpoint) : ZigbeeEP(endpoint) {
    _device_id = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID;
    _ep_config = {.endpoint = endpoint, .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
                  .app_device_id = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID, .app_device_version = 0};
    _cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(_cluster_list, esp_zb_basic_cluster_create(nullptr),
                                        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(_cluster_list, esp_zb_identify_cluster_create(nullptr),
                                           ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_analog_input_cluster_cfg_t cfg = {};
    cfg.present_value = NAN;  // unknown until a valid display packet arrives
    auto *cluster = esp_zb_analog_input_cluster_create(&cfg);
    char description[] = "\x10" "Desk height (cm)";
    // ZCL strings are length-prefixed; description is 16 bytes.
    uint16_t unit = 118;  // BACnet centimeters
    float resolution = DESK_DISPLAY_IN_INCHES ? 0.254f : 0.1f;
    float min = 20, max = 510;
    ESP_ERROR_CHECK(esp_zb_analog_input_cluster_add_attr(cluster,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_DESCRIPTION_ID, description));
    ESP_ERROR_CHECK(esp_zb_analog_input_cluster_add_attr(cluster,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_ENGINEERING_UNITS_ID, &unit));
    ESP_ERROR_CHECK(esp_zb_analog_input_cluster_add_attr(cluster,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_RESOLUTION_ID, &resolution));
    ESP_ERROR_CHECK(esp_zb_analog_input_cluster_add_attr(cluster,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_MIN_PRESENT_VALUE_ID, &min));
    ESP_ERROR_CHECK(esp_zb_analog_input_cluster_add_attr(cluster,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_MAX_PRESENT_VALUE_ID, &max));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_analog_input_cluster(_cluster_list, cluster,
                                                             ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
  }
  bool setHeight(float height) {
    if (!acquireDeskZigbeeLock()) return false;
    auto ret = esp_zb_zcl_set_attribute_val(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID, &height, false);
    esp_zb_lock_release();
    return ret == ESP_ZB_ZCL_STATUS_SUCCESS;
  }
  bool reportHeight() {
    esp_zb_zcl_report_attr_cmd_t report = {};
    report.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
    report.zcl_basic_cmd.src_endpoint = _endpoint;
    report.clusterID = ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT;
    report.attributeID = ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID;
    report.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
    report.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;
    if (!acquireDeskZigbeeLock()) return false;
    auto ret = esp_zb_zcl_report_attr_cmd_req(&report);
    esp_zb_lock_release();
    return ret == ESP_OK;
  }
};
