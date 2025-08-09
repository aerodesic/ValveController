/* Class of Zigbee On/Off Light endpoint inherited from common EP class */

#pragma once

#include "soc/soc_caps.h"
#include "sdkconfig.h"

#if CONFIG_ZB_ENABLED

#define ZIGBEE_LIGHT_ENDPOINT  10

#include "ZigbeeEP.h"
#include "ha/esp_zigbee_ha_standard.h"

class ValveController : public ZigbeeEP {
public:
  ValveController(uint8_t endpoint = ZIGBEE_LIGHT_ENDPOINT);
  ~ValveController() {}

  // Use to set a cb function to be called on light change
  void onValveChanged(void (*callback)(bool)) {
    _on_valve_changed = callback;
  }

  // Use to restore valve state
  void restoreValve() {
    ValveChanged();
  }

  // Use to control valve state
  bool setValve(bool state);
  
  // Use to get light state
  bool getValveState() const {
    return _current_state;
  }

  uint16_t getOnTime() const {
    return _on_time_value;
  }

  static void TurnOffCallback(TimerHandle_t timer);

  void StartTurnOffTimer(TickType_t period);
  
private:
  void zbAttributeSet(const esp_zb_zcl_set_attr_value_message_t *message) override;
  //callback function to be called on light change
  void (*_on_valve_changed)(bool);
  void ValveChanged();

  bool _current_state;
  uint16_t _on_time_value;
  TimerHandle_t _turn_off_timer;
};

#endif  // CONFIG_ZB_ENABLED
