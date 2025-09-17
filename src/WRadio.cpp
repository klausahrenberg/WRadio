#include <Arduino.h>

#include "WNetwork.h"
#include "WRadio.h"
#include "WM8978.h"
#include "html/WRadioPage.h"

WNetwork *network;
WRadio *radio;
//WBluetooth* bt;

void setup() {
  if (DEBUG) {
    Serial.begin(115200);
  }
  APPLICATION = "Radio";
  VERSION = "1.50";
  FLAG_SETTINGS = 0x23;
  DEBUG = true;  

  network = new WNetwork(NO_LED);
  radio = new WRadio(network);
  network->addDevice(radio);

  if (psramFound()) {
    LOG->debug(F("psram found %d"), ESP.getPsramSize());
  } else {
    LOG->debug(F("psram not found"));
  }

  //Web pages
  network->addWebPage("radio", [radio](){ return new WRadioPage(radio); }, PSTR("Radio"));
}

void loop() {
  network->loop(millis());
  delay(50);
}
