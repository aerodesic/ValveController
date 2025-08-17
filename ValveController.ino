// Copyright 2024 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/**
 * @brief This example demonstrates simple Zigbee light bulb.
 *
 * The example demonstrates how to use Zigbee library to create a end device light bulb.
 * The light bulb is a Zigbee end device, which is controlled by a Zigbee coordinator.
 *
 * Proper Zigbee mode must be selected in Tools->Zigbee mode
 * and also the correct partition scheme must be selected in Tools->Partition Scheme.
 *
 * Please check the README.md for instructions and more detailed description.
 *
 * Created by Jan Procházka (https://github.com/P-R-O-C-H-Y/)
 */

//
// Derived to provide a valve controller that will operate for a number of seconds.
// G. Oliver <go@aerodesic.com>

#ifndef ZIGBEE_MODE_ED
#error "Zigbee end device mode is not selected in Tools->Zigbee mode"
#endif

#define ENABLE_LIGHT_SLEEP         0
#define ENABLE_EXTERNAL_ANTENNA    0

#include "Zigbee.h"
#include "ValveController.h"
#include "esp_event.h"

#if ENABLE_LIGHT_SLEEP
#define uS_TO_S_FACTOR     1000000ULL /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP      1          /* In seconds */
#define TIME_TO_STAY_AWAKE 60         /* In seconds */
#endif


ValveController zbValve = ValveController();

bool _currentValveState = false;

/********************* RGB LED functions **************************/
void setLED(bool value) {
  digitalWrite(LED_BUILTIN, !value);
}

TaskHandle_t valveThreadHandle;
QueueHandle_t valveCommandQueue;

#define TOGGLE_PIN    2
#define OPEN_PIN      21
#define CLOSE_PIN     22
#define POWER_PIN     23
#define IDENTIFY_PIN  16

typedef enum {
  CLOSE_VALVE,
  OPEN_VALVE,
  IDENTIFY_VALVE
} ValveCommand_t;

typedef struct __ValveMessage_struct {
  ValveCommand_t command;
  uint16_t parameter;
} ValveMessage_t;

// For some reason this function prototype is required else I get an undefined
// ValveCommand_t in the function definition.  Even declaring the function as
// 'static' doesn't fix the issue.
void sendValveMessage(ValveCommand_t command, uint16_t parameter);

void sendValveMessage(ValveCommand_t command, uint16_t parameter) {
  ValveMessage_t message = {.command = command, .parameter = parameter };

  if(xQueueSend(valveCommandQueue, (void *) &message, (TickType_t) 10) != pdPASS) {
    log_e("sendValveMessage could not send to queue!");
  }
}

// Valve handler thread - this thread never ends
void valveThread(void *) {
  unsigned long start = millis();

  for (;;) {
    // Wait for a message on queue
    ValveMessage_t message;

    // Attempt to receive an integer from queue, waiting for a maximum of 10 ticks if the queue is empty
    if (xQueueReceive(valveCommandQueue, (void *) &message, 10) == pdPASS) {
      // Item successfully received, process command
      log_v("valveThread: command %u parameter %u", message.command, message.parameter);
      switch (message.command) {
        case CLOSE_VALVE: {
          // Close valve
          digitalWrite(POWER_PIN, HIGH);
          digitalWrite(CLOSE_PIN, HIGH);
          delay(5000);
          digitalWrite(CLOSE_PIN, LOW);
          digitalWrite(POWER_PIN, LOW);
          break;
        }  
        case OPEN_VALVE: {
          // Open valve
          digitalWrite(POWER_PIN, HIGH);
          digitalWrite(OPEN_PIN, HIGH);
          delay(5000);
          digitalWrite(OPEN_PIN, LOW);
          digitalWrite(POWER_PIN, LOW);
          break;
        }
        case IDENTIFY_VALVE: {
          uint16_t count = message.parameter;
          while(count != 0) {
            digitalWrite(IDENTIFY_PIN, HIGH);
            delay(250);
            digitalWrite(IDENTIFY_PIN, LOW);
            delay(250);
            digitalWrite(IDENTIFY_PIN, HIGH);
            delay(250);
            digitalWrite(IDENTIFY_PIN, LOW);
            delay(250);
            --count;
          }
          break;
        }
        default: {
          break;
        }
      }
      // Restart SLEEP start any time we do something.
      start = millis();
#if ENABLE_LIGHT_SLEEP
    } else if ((millis() - start) >= TIME_TO_STAY_AWAKE*1000) {
      // No work to do so sleepy-bye
      log_v("Sleeping");
      esp_light_sleep_start();
      log_v("Wakeup");
      //esp_zb_start(true);
      // Zigbee.scanNetworks();
      zbValve.setValve(_currentValveState);
      start = millis();
#endif
    } else {
      // No item received within the specified timeout, or queue was empty
      delay(50);
    }
  }
}

void valveChanged(bool value) {
  _currentValveState = value;
  setLED(value);
  log_v("valveChanged: %s, on_time %d\n", value ? "true" : "false", zbValve.getOnTime());
  sendValveMessage(value ? OPEN_VALVE : CLOSE_VALVE, 0);
}

void valveIdentify(uint16_t time_in_seconds) {
  sendValveMessage(IDENTIFY_VALVE, time_in_seconds);
}

int togglePinCounter = 0;
void IRAM_ATTR togglePinInterrupt() { // TOGGLE PIN interrupts
  togglePinCounter++;
}

// ESP_EVENT_DECLARE_BASE(WIFI_EVENT);

void my_zigbee_signal_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  // Cast the event_data to esp_zb_app_signal_t
  esp_zb_app_signal_t *signal_struct = (esp_zb_app_signal_t *)event_data;

  // Retrieve signal type and error status
  esp_zb_app_signal_type_t sig_type = (esp_zb_app_signal_type_t) *signal_struct->p_app_signal; 
  esp_err_t err_status = signal_struct->esp_err_status;

  switch(sig_type) {
    case ESP_ZB_COMMON_SIGNAL_CAN_SLEEP: {
      log_v("'can sleep' received; event_base %u event_id %d", event_base, event_id);
      break;
    }
    default: {
      log_v("ZDO signal: %s (0x%x), event_base %u status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type, event_base, esp_err_to_name(err_status));
      break;
    }
  }
}

/********************* Arduino functions **************************/
void setup() {
  Serial.begin(115200);

#if ENABLE_LIGHT_SLEEP
  esp_zb_sleep_enable(true);
#endif

  // Init LED and turn it OFF (if LED_PIN == RGB_BUILTIN, the rgbLedWrite() will be used under the hood)
  pinMode(BOOT_PIN, INPUT_PULLUP);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Set mode for valve control pins
  pinMode(POWER_PIN, OUTPUT);
  digitalWrite(POWER_PIN, LOW);
  pinMode(OPEN_PIN, OUTPUT);
  digitalWrite(OPEN_PIN, LOW);
  pinMode(CLOSE_PIN, OUTPUT);
  digitalWrite(CLOSE_PIN, LOW);
  pinMode(IDENTIFY_PIN, OUTPUT);
  digitalWrite(IDENTIFY_PIN, LOW);
  
  pinMode(TOGGLE_PIN, INPUT_PULLUP); // Configure the pin with internal pull-up
  attachInterrupt(digitalPinToInterrupt(TOGGLE_PIN), togglePinInterrupt, FALLING); // Trigger on falling edge

  // Catch app signals.
  esp_event_handler_instance_register(ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID, my_zigbee_signal_handler, NULL, NULL);

#if ENABLE_LIGHT_SLEEP
  // Configure the wake up source and set to wake up every so often
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
#endif

  //Optional: set Zigbee device name and model
  zbValve.setManufacturerAndModel("Aerodesic", "Valve");

  // Notify it is battery powered
  zbValve.setPowerSource(ZB_POWER_SOURCE_BATTERY, 100, 35);
  
  // Set callback function for light change
  zbValve.onValveChanged(valveChanged);

  // Handle identify requests
  zbValve.onIdentify(valveIdentify);

  //Zigbee.setRxOnWhenIdle(false);

  // Start a thread to manage the opening and closing of the valve
  valveCommandQueue = xQueueCreate(5, sizeof(ValveMessage_t));
  if (valveCommandQueue == NULL) {
    log_e("Valve control control queue could not be created!");
    log_e("Rebooting...");
    ESP.restart();

  } else if (xTaskCreate(valveThread, "ValveControl", 10000, NULL, 0, &valveThreadHandle) != pdPASS) {
    // Failed - reboot and try again
    log_e("Valve control thread failed to start!");
    log_e("Rebooting...");
    ESP.restart();
  } else {
    // Send command to close the valve
    valveChanged(false);

    //Add endpoint to Zigbee Core
    log_v("Adding ValveController endpoint to Zigbee Core");
    Zigbee.addEndpoint(&zbValve);

#if ENABLE_EXTERNAL_ANTENNA
    // Enabling external antenna
    pinMode(3, OUTPUT);
    digitalWrite(3, LOW);//turn on this function
    delay(100);
    pinMode(14, OUTPUT);
    digitalWrite(14, HIGH);//use external antenna
    log_v("External antenna enabled");
#endif

    if (!Zigbee.begin()) {
      log_e("Zigbee failed to start!");
      log_e("Rebooting...");
      ESP.restart();
    }

    log_v("Connecting to network");

    while (!Zigbee.connected()) {
      Serial.write('.');
      delay(100);
    }

    Serial.println();
  }
}

void loop() {
  // Checking button for factory reset
  if (digitalRead(BOOT_PIN) == LOW) {  // Push button pressed
    // Key debounce handling
    delay(100);
    int startTime = millis();
    while (digitalRead(BOOT_PIN) == LOW) {
      delay(50);
      if ((millis() - startTime) > 3000) {
        // If key pressed for more than 3secs, factory reset Zigbee and reboot
        log_v("Resetting Zigbee to factory and rebooting in 1s.");
        delay(1000);
        Zigbee.factoryReset();
        Serial.println("Going to endless sleep, press RESET button or power off/on the device to wake up");
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
        esp_deep_sleep_start();
      }
    }
  }

  if (togglePinCounter != 0) {
    // Ignore until half second after startup.
    if (millis() > 500) {
      log_v("togglePinCounter %d", togglePinCounter);
      // Toggle valve by pressing the button
      zbValve.setValve(!zbValve.getValveState());
    }
    togglePinCounter = 0;
  }

  delay(100);

  // Enter Light Sleep mode
  //esp_light_sleep_start();
}
