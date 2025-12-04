/*#include "Arduino.h"
#include "WiFi.h"
#include "Audio.h"

// Digital I/O used
#define WM8978_I2S_BCK 25
#define WM8978_I2S_LRC 27
#define WM8978_I2S_DOUT 26
#define XSMT 15

String ssid =     "Spirit";
String password = "1036699172512545";

Audio audio;

// callbacks
void my_audio_info(Audio::msg_t m) {
    Serial.printf("%s: %s\n", m.s, m.msg);
}

void setup() {
    Audio::audio_info_callback = my_audio_info; // optional
    Serial.begin(115200);
    delay(500);
    Serial.println("wifi.");
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.println("wifi ok.");
    if (psramFound()) {
    Serial.printf("psram found %d"), ESP.getPsramSize();
  } else {
    Serial.printf("psram not found");
  }
    while (WiFi.status() != WL_CONNECTED) delay(1500);
    //setPinout(uint8_t BCLK, uint8_t LRC, uint8_t DOUT, int8_t MCLK) {
    audio.setPinout(WM8978_I2S_BCK, WM8978_I2S_LRC, WM8978_I2S_DOUT);
    audio.setVolume(21); // default 0...21
    audio.connecttohost("http://stream.104.6rtl.com/rtl-live/mp3-192/play.m3u");
    delay(200);
    pinMode(XSMT, OUTPUT);
    delay(200);
    digitalWrite(XSMT, HIGH);
}

void loop(){
    audio.loop();
    vTaskDelay(1);
}*/


#include <Arduino.h>

#include "WNetwork.h"
#include "WRadio.h"
#include "WM8978.h"
#include "html/WRadioPage.h"

WNetwork *network;
WRadio *radio;
//WBluetooth* bt;

void setup() {
  APPLICATION = "Radio";
  VERSION = "1.50";
  FLAG_SETTINGS = 0x24;
  DEBUG = true;
  if (DEBUG) {
    Serial.begin(115200);
  }  

  network = new WNetwork(NO_LED);
  radio = new WRadio(network);
  network->addDevice(radio);

  
  if (psramFound()) {
    Serial.println("psram found %d"), ESP.getPsramSize();
    psramInit();
  } else {
    Serial.println("psram not found");
  }

  //Web pages
  network->addWebPage("radio", [radio](){ return new WRadioPage(radio); }, PSTR("Radio"));
}

void loop() {
  network->loop(millis());
  delay(50);
}
