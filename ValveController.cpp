#include "ValveController.h"
#if CONFIG_ZB_ENABLED

#define DEFAULT_ON_TIME_VALUE 3600   // .1 hour (6 min) hour as .1 second units

ValveController::ValveController(uint8_t endpoint) 
: ZigbeeEP(endpoint)
, _on_time_value(DEFAULT_ON_TIME_VALUE)
, _turn_off_timer(NULL) {
  _device_id = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID;

  esp_zb_on_off_light_cfg_t light_cfg = ESP_ZB_DEFAULT_ON_OFF_LIGHT_CONFIG();

  esp_zb_cluster_list_t *cluster_list = esp_zb_on_off_light_clusters_create(&light_cfg);
  
  esp_zb_endpoint_config_t ep_config = {
    .endpoint = endpoint,
    .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
    .app_device_id = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID,
    .app_device_version = 0
  };

#if 1
  // Get the onoff cluster
  esp_zb_attribute_list_t *on_off_attr_list = esp_zb_cluster_list_get_cluster(cluster_list, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  if (on_off_attr_list == NULL) {
    log_e("ValveController: on_off_attr_list is NULL");
  } else {
    // Add ON_TIME attribute to the On/Off cluster
    esp_err_t err = esp_zb_on_off_cluster_add_attr(on_off_attr_list, ESP_ZB_ZCL_ATTR_ON_OFF_ON_TIME, &_on_time_value);
    if (err != 0) {
      log_e("ValveController: error %d adding ON_TIME attribute\n", err);
    }
  }
#endif

  log_v("ValveController endpoint created %d\n", _endpoint);

  setEpConfig(ep_config, cluster_list);
}

void ValveController::TurnOffCallback(TimerHandle_t timer) {
  ValveController* valve = (ValveController *) pvTimerGetTimerID(timer);

  valve->setValve(false);
}

void ValveController::StartTurnOffTimer(TickType_t period) {
  if (period == 0) {
    if (_turn_off_timer != NULL) {
      xTimerStop(_turn_off_timer, 0);
    }
  } else {
    // Create the timer if it hasn't been created yet
    if (_turn_off_timer == NULL) {
        _turn_off_timer = xTimerCreate(
                            "TurnOffTimer",             // A text name for the timer, used for debugging.
                            period,                     // The period of the timer in ticks.
                            pdFALSE,                    // uxAutoReload set to pdFALSE for a one-shot timer.
                            this,                       // Pass address of Valve class
                            TurnOffCallback             // The function to call when the timer expires.
                        );
        configASSERT(_turn_off_timer); // Ensure the timer was created successfully
    }

    // Change the period of the existing timer
    BaseType_t xResult = xTimerChangePeriod(_turn_off_timer, period, 0); // xTicksToWait set to 0 for no blocking.
    configASSERT(xResult == pdPASS); // Ensure the period change was successful
  }
}

//set attribute method -> method overridden in child class
void ValveController::zbAttributeSet(const esp_zb_zcl_set_attr_value_message_t *message) {
  //check the data and call right method
  bool changed = false;

  if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
    if (message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
      _current_state = *(bool *)message->attribute.data.value;
      changed = true;
    } else if (message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_TIME && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U16) {
      _on_time_value = *(uint16_t *)message->attribute.data.value;
      changed = true;
    } else {
      log_w("Received message ignored. Attribute ID: %d not supported for On/Off Light", message->attribute.id);
    }
  } else {
    log_w("Received message ignored. Cluster ID: %d not supported for On/Off Light", message->info.cluster);
  }
  if (changed) {
    ValveChanged();
  }
}
ValveController::zbAttributeGet(){
  
}
void ValveController::ValveChanged() {
  // First do handling of the on_time value.
  if (_current_state) {
    // on_in terms of 0.1 seconds so we convert to milliseconds before converting to ticks.
    if (_on_time_value != 0) {
      StartTurnOffTimer((_on_time_value * 100) / portTICK_PERIOD_MS);
    }
  } else {
    StartTurnOffTimer(0);  // Stop timer
  }
  if (_on_valve_changed) {
    _on_valve_changed(_current_state);
  } else {
    log_w("No callback function set for light change");
  }
}

bool ValveController::setValve(bool state) {

  if (state != _current_state) {
    _current_state = state;
    ValveChanged();
  }
  
  log_v("Updating on/off valve state to %d", state);

  /* Update on/off state */
  esp_zb_lock_acquire(portMAX_DELAY);
  esp_zb_zcl_status_t ret = esp_zb_zcl_set_attribute_val(
    getEndpoint(),
    ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
    ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
    &_current_state,
    false
  );
  esp_zb_lock_release();

  if (ret != ESP_ZB_ZCL_STATUS_SUCCESS) {
    log_e("Failed to set valve state: 0x%x: %s", ret, esp_zb_zcl_status_to_name(ret));
    return false;
  }
  return true;
}

#endif  // CONFIG_ZB_ENABLED
