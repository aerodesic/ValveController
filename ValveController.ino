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

#include "Zigbee.h"
#include "ValveController.h"

/* Zigbee light bulb configuration */

uint8_t led = LED_BUILTIN;
uint8_t button = BOOT_PIN;

ValveController zbValve = ValveController();

/********************* RGB LED functions **************************/
void setLED(bool value) {
  digitalWrite(led, !value);
}

void valveChanged(bool value) {
  setLED(value);
  log_v("valveChanged: %s, on_time %d\n", value ? "true" : "false", zbValve.getOnTime());
}

/********************* Arduino functions **************************/
void setup() {
  Serial.begin(115200);

  // Init LED and turn it OFF (if LED_PIN == RGB_BUILTIN, the rgbLedWrite() will be used under the hood)
  pinMode(led, OUTPUT);
  digitalWrite(led, LOW);

  // Init button for factory reset
  pinMode(button, INPUT_PULLUP);

  //Optional: set Zigbee device name and model
  zbValve.setManufacturerAndModel("Aerodesic", "Valve");

  // Notify it is battery powered
  //zbValve.setPowerSource(ZB_POWER_SOURCE_BATTERY, 100, 35);
  zbValve.setPowerSource(ZB_POWER_SOURCE_MAINS, 100, 35);
  
  // Set callback function for light change
  zbValve.onValveChanged(valveChanged);

  //Add endpoint to Zigbee Core
  log_v("Adding ValveController endpoint to Zigbee Core");
  Zigbee.addEndpoint(&zbValve);

#if 1
  // Enabling external antenna
  pinMode(3, OUTPUT);
  digitalWrite(3, LOW);//turn on this function
  delay(100);
  pinMode(14, OUTPUT);
  digitalWrite(14, HIGH);//use external antenna
  log_v("External antenna enabled");
#endif

  //Open network for 180 seconds after boot
  //Zigbee.setRebootOpenNetwork(180);

  // When all EPs are registered, start Zigbee. By default acts as ZIGBEE_END_DEVICE
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

void loop() {
  // Checking button for factory reset
  if (digitalRead(button) == LOW) {  // Push button pressed
    // Key debounce handling
    delay(100);
    int startTime = millis();
    while (digitalRead(button) == LOW) {
      delay(50);
      if ((millis() - startTime) > 3000) {
        // If key pressed for more than 3secs, factory reset Zigbee and reboot
        log_v("Resetting Zigbee to factory and rebooting in 1s.");
        delay(1000);
        Zigbee.factoryReset();
      }
    }
    // Toggle valve by pressing the button
    zbValve.setValve(!zbValve.getValveState());
  }
  delay(100);
}
