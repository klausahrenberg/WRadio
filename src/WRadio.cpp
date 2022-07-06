#include "WRadio.h"
#include <Arduino.h>
#include "WNetwork.h"

#define APPLICATION "Radio"
#define VERSION "1.25"
#define FLAG_SETTINGS 0x19
#define DEBUG false

#define PIN_STATUS_LED 22
#define PIN_POWER_BUTTON 39

WNetwork *network;
WRadio *radio;

void setup() {
  if (DEBUG) {
    Serial.begin(9600);
  }
  network =
      new WNetwork(DEBUG, APPLICATION, VERSION, PIN_STATUS_LED, FLAG_SETTINGS);
  radio = new WRadio(network, PIN_POWER_BUTTON);
  network->addDevice(radio);
}

void loop() {
  network->loop(millis());
  delay(50);
}
