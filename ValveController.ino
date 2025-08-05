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

TaskHandle_t valveThreadHandle;
QueueHandle_t valveCommandQueue;

#define OPEN_PIN    21
#define CLOSE_PIN   22
#define POWER_PIN   23

// Valve handle thread - this thread never ends
void valveThread(void *) {
  for (;;) {
    // Wait for a message on queue
    uint8_t command;

    // Attempt to receive an integer from myQueue, waiting for a maximum of 10 ticks if the queue is empty
    if (xQueueReceive(valveCommandQueue, &command, 10) == pdPASS)
    {
      // Item successfully received, process command
      if (command == 0) {
        // Close valve
        digitalWrite(POWER_PIN, HIGH);
        digitalWrite(CLOSE_PIN, HIGH);
        delay(5000);
        digitalWrite(CLOSE_PIN, LOW);
        digitalWrite(POWER_PIN, LOW);
    
      } else {
        // Open valve
        digitalWrite(POWER_PIN, HIGH);
        digitalWrite(OPEN_PIN, HIGH);
        delay(5000);
        digitalWrite(OPEN_PIN, LOW);
        digitalWrite(POWER_PIN, LOW);
      }
    }
    else
    {
        // No item received within the specified timeout, or queue was empty
      delay(50);
    }
  }
}

void valveChanged(bool value) {
  setLED(value);
  log_v("valveChanged: %s, on_time %d\n", value ? "true" : "false", zbValve.getOnTime());
  // Send command to thread
  uint8_t command = value ? 1 : 0;
  if( xQueueSend(valveCommandQueue, &command, (TickType_t) 10) != pdPASS) {
    log_e("valveChange could not send to queue!");
  }
}

/********************* Arduino functions **************************/
void setup() {
  Serial.begin(115200);

  // Init LED and turn it OFF (if LED_PIN == RGB_BUILTIN, the rgbLedWrite() will be used under the hood)
  pinMode(led, OUTPUT);
  digitalWrite(led, LOW);

  // Set mode for valve control pins
  pinMode(POWER_PIN, OUTPUT);
  digitalWrite(POWER_PIN, LOW);
  pinMode(OPEN_PIN, OUTPUT);
  digitalWrite(OPEN_PIN, LOW);
  pinMode(CLOSE_PIN, OUTPUT);
  digitalWrite(CLOSE_PIN, LOW);

  // Init button for factory reset
  pinMode(button, INPUT_PULLUP);

  //Optional: set Zigbee device name and model
  zbValve.setManufacturerAndModel("Aerodesic", "Valve");

  // Notify it is battery powered
  zbValve.setPowerSource(ZB_POWER_SOURCE_BATTERY, 100, 35);
  //zbValve.setPowerSource(ZB_POWER_SOURCE_MAINS, 100, 35);
  
  // Set callback function for light change
  zbValve.onValveChanged(valveChanged);

  // Start a thread to manage the opening and closing of the valve
  valveCommandQueue = xQueueCreate(10, sizeof( uint8_t ));
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
    uint8_t close_command = 0;
    xQueueSend(valveCommandQueue, &close_command, (TickType_t) 10);

    //Add endpoint to Zigbee Core
    log_v("Adding ValveController endpoint to Zigbee Core");
    Zigbee.addEndpoint(&zbValve);

#if 0
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
