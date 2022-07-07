#ifndef W_RADIO_H
#define W_RADIO_H

#include <Arduino.h>
#include "WNetwork.h"
#include "WAudio.h"
#include "WBluetooth.h"
#include "WDevice.h"
#include "WM8978.h"
#include "WSwitch.h"

#define DEVICE_ID "radio"
#define MAX_STATIONS 12
const char* DEFAULT_STATION_TITLE = "_^t_%d";
const char* DEFAULT_STATION_URL = "_^u_%d";
#define CAPTION_ADD "Add"
#define CAPTION_EDIT "Edit"
#define CAPTION_REMOVE "Remove"
#define CAPTION_OK "Ok"
#define CAPTION_CANCEL "Cancel"
#define HTTP_EDIT_STATION "editstation"
#define HTTP_REMOVE_STATION "removestation"
const static char HTTP_BUTTON_VALUE[] PROGMEM = R"=====(
	<div>
  	<form action='/%s' method='POST'>
    	<button class='%s' name='station' value='%s'>%s</button>
    </form>
  </div>
)=====";
const static char* SOURCE_BLUETOOTH = "Bluetooth";
const static char* POWER_OFF = "<off>";

// T-Audio 1.6 WM8978 I2C pins.
#define WM8978_I2C_SDA 19
#define WM8978_I2C_SCL 18
// T-Audio 1.6 WM8978 I2S pins.
#define WM8978_I2S_BCK 33
#define WM8978_I2S_WS 25
#define WM8978_I2S_DOUT 26
#define WM8978_I2S_DIN 27
// T-Audio 1.6 WM8978 MCLK gpio number?
#define WM8978_I2S_MCLKPIN GPIO_NUM_0

class WRadio : public WDevice {
 public:
  WRadio(WNetwork* network, byte intPowerButton, byte intSourceButton)
      : WDevice(network, DEVICE_ID, network->getIdx(), DEVICE_TYPE_RADIO,
                DEVICE_TYPE_ON_OFF_SWITCH) {
    this->radio = nullptr;
    this->bt = nullptr;
    this->dac = new WM8978();
    /* Setup wm8978 I2C interface */
    if (!dac->begin(WM8978_I2C_SDA, WM8978_I2C_SCL)) {
      log_e("Error setting up dac.");
    }
    // On
    this->onProperty = WProperty::createOnOffProperty("on", "Power");
    this->onProperty->setBoolean(false);
    network->getSettings()->add(this->onProperty);
    this->onProperty->setOnChange(
        std::bind(&WRadio::onPropertyChanged, this, std::placeholders::_1));
    this->addProperty(onProperty);
    // Volume
    this->volume = new WLevelIntProperty("volume", "Volume", 0, 20);
    this->volume->setInteger(20);
    network->getSettings()->add(this->volume);
    this->volume->setOnChange(
        std::bind(&WRadio::volumeChanged, this, std::placeholders::_1));
    this->addProperty(this->volume);
    this->dac->setHPvol(0, 0);
    // BT device name
    this->btDeviceName =
        WProperty::createStringProperty("btDeviceName", "Device Name");
    this->btDeviceName->setString(network->getIdx());
    network->getSettings()->add(this->btDeviceName);
    this->btDeviceName->setOnChange(
        std::bind(&WRadio::btDeviceNameChanged, this, std::placeholders::_1));
    this->addProperty(this->btDeviceName);
    // Streamtext
    this->streamtitle = WProperty::createStringProperty("streamtitle", "Title");
    this->addProperty(this->streamtitle);
    // Station Memory
    this->editingTitle = nullptr;
    this->editingUrl = nullptr;
    this->numberOfStations =
        network->getSettings()->setByte("numberOfStations", 0, MAX_STATIONS);
    this->numberOfStations->setVisibility(NONE);
    this->addProperty(this->numberOfStations);
    this->station = network->getSettings()->setString("station", "");
    this->addProperty(this->station);
    // Configure Device
    configureDevice();
    this->station->setOnChange(
        std::bind(&WRadio::stationChanged, this, std::placeholders::_1));

    // Power switch
    WSwitch* powerButton = new WSwitch(intPowerButton, MODE_BUTTON);
    powerButton->setProperty(this->onProperty);
    this->addPin(powerButton);
    //Source switch
    WSwitch* sourceButton = new WSwitch(intSourceButton, MODE_BUTTON);
    //sourceButton->setProperty(this->onProperty);
    sourceButton->setOnPressed([this] {
      byte i = this->station->getEnumIndex();
      i++;
      if (i > this->numberOfStations->getByte()) {
        i = 0;
      }
      this->station->setString(this->station->getEnumString(i));      
    });
    this->addPin(sourceButton);

    // HtmlPages
    WPage* configPage = new WPage(this->getId(), "Configure radio");
    configPage->setPrintPage(std::bind(&WRadio::printConfigPage, this,
                                       std::placeholders::_1,
                                       std::placeholders::_2));
    configPage->setSubmittedPage(std::bind(&WRadio::saveConfigPage, this,
                                           std::placeholders::_1,
                                           std::placeholders::_2));
    network->addCustomPage(configPage);
    // Add/Edit station
    WPage* stationPage = new WPage(HTTP_EDIT_STATION, "Add/edit station");
    stationPage->setShowInMainMenu(false);
    stationPage->setPrintPage(std::bind(&WRadio::handleHttpEditStation, this,
                                        std::placeholders::_1,
                                        std::placeholders::_2));
    stationPage->setSubmittedPage(
        std::bind(&WRadio::handleHttpSubmitEditStation, this,
                  std::placeholders::_1, std::placeholders::_2));
    stationPage->setTargetAfterSubmitting(configPage);
    network->addCustomPage(stationPage);
    // Remove station
    WPage* removeStation = new WPage(HTTP_REMOVE_STATION, "Remove station");
    removeStation->setShowInMainMenu(false);
    removeStation->setPrintPage(
        std::bind(&WRadio::handleHttpRemoveStationButton, this,
                  std::placeholders::_1, std::placeholders::_2));
    network->addCustomPage(removeStation);
  }

  void play() {
    if (!starting) {
      starting = true;
      byte i = this->station->getEnumIndex();
      if (i == 0) {
        if (this->bt == nullptr) {                    
          this->streamtitle->setString(SOURCE_BLUETOOTH);
          log_i("Bluetooth on");
          delay(100);
          stop();
          this->bt = new WBluetooth(this->btDeviceName->c_str());
          this->bt->setOnChange([this]() {
            if (this->bt->isConnected()) {
              if (this->bt->isPlaying()) {
                this->streamtitle->setString(this->bt->getStreamTitle().c_str());
              } else {
                this->streamtitle->setString(this->bt->getRemoteName().c_str());
              }
            } else {
              this->streamtitle->setString(SOURCE_BLUETOOTH);
            }
          });
          this->bt->init(WM8978_I2S_BCK, WM8978_I2S_WS, WM8978_I2S_DOUT,
                         WM8978_I2S_MCLKPIN);
          //this->bt->play();
          unMute();
        }
      } else if (i > 0) {
        if ((this->radio == nullptr) && (network->isWifiConnected())) {                    
          this->streamtitle->setString(this->getStationTitle(i - 1)->c_str());
          log_i("Radio on");
          delay(100);
          stop();
          this->radio = new WAudio();
          this->radio->setOnChange([this]() {
            this->streamtitle->setString(this->radio->getStreamTitle().c_str());
          });
          this->radio->init(WM8978_I2S_BCK, WM8978_I2S_WS, WM8978_I2S_DOUT,
                            WM8978_I2S_MCLKPIN);
          if (!this->radio->play(this->getStationUrl(i - 1)->c_str())) {
            network->debug(F("Can't connect to '%s'"),
                           this->getStationUrl(i - 1)->c_str());
            stop();
          }
          if (!this->radio->isRunning()) {
            network->debug(F("Radio not running, reason unknown"));
            stop();
          } else {
            unMute();
          }
        }
      } else {
        log_i("No radio stations stored for playing");
      }
      starting = false;
    }
  }

  void stop() {
    if (!stopping) {
      stopping = true;
      if (this->radio != nullptr) {
        log_i("Radio off");
        mute();
        this->radio->stopSong();
        delay(50);
        delete this->radio;
        this->radio = nullptr;
      }
      if (this->bt != nullptr) {
        log_i("Bluetooth off");
        mute();
        this->bt->stop();
        delay(50);
        delete this->bt;
        this->bt = nullptr;
      }
      stopping = false;
    }
  }

  void mute() {
    byte v = this->volume->getInteger() + 40;
    while (v > 40) {
      this->dac->setHPvol(v, v);
      v--;
      delay(30);
    }
    this->dac->setHPvol(0, 0);
  }

  void unMute() {
    byte v = min(40, this->volume->getInteger() + 40);
    this->dac->setHPvol(v, v);
    while (v < this->volume->getInteger() + 40) {
      v++;
      this->dac->setHPvol(v, v);
      delay(30);
    }
  }

  void loop(unsigned long now) {
    WDevice::loop(now);
    if (onProperty->getBoolean()) {
      play();
    } else {
      stop();
    }
  }

  WProperty* getStreamTitle() { return this->streamtitle; }

  void handleHttpRemoveStationButton(AsyncWebServerRequest* request,
                                     Print* page) {
    bool exists = (request->hasParam("station", true));
    if (exists) {
      String sIndex = request->getParam("station", true)->value();
      int index = atoi(sIndex.c_str());
      this->removeStationConfig(index);
      // moveProperty(index + 1, MAX_GPIOS);
      page->print("Deleted item: ");
      page->print(sIndex);
    } else {
      page->print("Missing station parameter. Nothing deleted.");
    }
    page->printf(HTTP_BUTTON, DEVICE_ID, "get", "Go back");
  }

  virtual void printConfigPage(AsyncWebServerRequest* request, Print* page) {
    // Table with already stored MAX_GPIOS
    page->printf(HTTP_DIV_ID_BEGIN, "gd");
    page->print(F("<table  class='st'>"));
    tr(page);
    th(page);
    page->print("No.");
    thEnd(page);
    th(page);
    page->print("Name");
    thEnd(page);
    th(page); /*Edit*/
    if (numberOfStations->getByte() < MAX_STATIONS)
      page->printf(HTTP_BUTTON, HTTP_EDIT_STATION, "get", CAPTION_ADD);
    thEnd(page);
    th(page); /*Remove*/
    thEnd(page);
    trEnd(page);
    char* pNumber = new char[2];
    for (byte i = 0; i < this->numberOfStations->getByte(); i++) {
      WProperty* stationTitle = getStationTitle(i);
      sprintf(pNumber, "%d", i);
      tr(page);
      td(page);
      page->print(pNumber);
      tdEnd(page);
      td(page);
      page->print(stationTitle->c_str());
      tdEnd(page);
      td(page);
      page->printf(HTTP_BUTTON_VALUE, HTTP_EDIT_STATION, "", pNumber,
                   CAPTION_EDIT);
      tdEnd(page);
      td(page);
      page->printf(HTTP_BUTTON_VALUE, HTTP_REMOVE_STATION, "cbtn", pNumber,
                   CAPTION_REMOVE);
      tdEnd(page);
      trEnd(page);
    }
    page->print(F("</table>"));
    page->printf(HTTP_DIV_END);

    page->printf(HTTP_CONFIG_PAGE_BEGIN, getId());
    page->print(FPSTR(HTTP_CONFIG_SAVE_BUTTON));
  }

  void handleHttpEditStation(AsyncWebServerRequest* request, Print* page) {
    page->printf(HTTP_CONFIG_PAGE_BEGIN, HTTP_EDIT_STATION);
    updateEditingStation(request, page);
    // Station title
    page->printf(
        HTTP_TEXT_FIELD, "Title:", "st", "12",
        (this->editingTitle != nullptr ? this->editingTitle->c_str() : ""));
    // Station URL
    page->printf(
        HTTP_TEXT_FIELD, "URL:", "su", "64",
        (this->editingUrl != nullptr ? this->editingUrl->c_str() : ""));
    page->printf(HTTP_BUTTON_SUBMIT, CAPTION_OK);
    page->printf(HTTP_BUTTON, DEVICE_ID, "get", CAPTION_CANCEL);
  }

  void handleHttpSubmitEditStation(AsyncWebServerRequest* request,
                                   Print* page) {
    addStationConfigItem(request->arg("st"), request->arg("su"));
  }

  void saveConfigPage(AsyncWebServerRequest* request, Print* page) {}

 protected:
  WProperty* getStationTitle(byte index) {
    char* pNumber = new char[6];
    sprintf(pNumber, DEFAULT_STATION_TITLE, index);
    return network->getSettings()->getSetting(pNumber);
  }

  WProperty* getStationUrl(byte index) {
    char* pNumber = new char[6];
    sprintf(pNumber, DEFAULT_STATION_URL, index);
    return network->getSettings()->getSetting(pNumber);
  }

  WProperty* addStationConfig() {
    byte index = this->numberOfStations->getByte();
    char* pNumber = new char[6];
    // Title
    sprintf(pNumber, DEFAULT_STATION_TITLE, index);
    WProperty* station = network->getSettings()->setString(pNumber, "");
    // Url
    sprintf(pNumber, DEFAULT_STATION_URL, index);
    network->getSettings()->setString(pNumber, "");
    return station;
  }

  void removeStationConfig(byte index) {
    char* pNumber = new char[6];
    // Title
    sprintf(pNumber, DEFAULT_STATION_TITLE, index);
    network->getSettings()->remove(pNumber);
    // Url
    sprintf(pNumber, DEFAULT_STATION_URL, index);
    network->getSettings()->remove(pNumber);
    char* p2 = new char[6];
    for (int i = index + 1; i < this->numberOfStations->getByte(); i++) {
      // Title
      sprintf(pNumber, DEFAULT_STATION_TITLE, i);
      sprintf(p2, DEFAULT_STATION_TITLE, i - 1);
      network->getSettings()->getSetting(pNumber)->setId(p2);
      // Url
      sprintf(pNumber, DEFAULT_STATION_URL, i);
      sprintf(p2, DEFAULT_STATION_URL, i - 1);
      network->getSettings()->getSetting(pNumber)->setId(p2);
    }
    this->numberOfStations->setByte(this->numberOfStations->getByte() - 1);
  }

  void updateEditingStation(AsyncWebServerRequest* request, Print* page) {
    bool exists = (request->hasParam("station", true));
    if (!exists) {
      page->print("New station");
      clearEditingStation();
    } else {
      String sIndex = request->getParam("station", true)->value();
      int index = atoi(sIndex.c_str());
      page->print("Edit station: ");
      page->print(sIndex);
      this->editingTitle = getStationTitle(index);
      this->editingUrl = getStationUrl(index);
    }
  }

  void ensureEditingStation() {
    if (this->editingTitle == nullptr) {
      this->editingTitle = this->addStationConfig();
      this->editingUrl = getStationUrl(this->numberOfStations->getByte());
      this->numberOfStations->setByte(this->numberOfStations->getByte() + 1);
    }
  }

  void onPropertyChanged(WProperty* property) {
    network->getSettings()->save();
    if (!this->onProperty->getBoolean()) {
      this->streamtitle->setString(POWER_OFF);
    }
  }

  void stationChanged(WProperty* property) {
    network->getSettings()->save();
    delay(100);
    stop();
    this->streamtitle->setString(this->station->c_str());
    this->play();
  }

  void volumeChanged(WProperty* property) {
    network->getSettings()->save();
    this->dac->setHPvol(this->volume->getInteger() + 40,
                        this->volume->getInteger() + 40);
  }

  void btDeviceNameChanged(WProperty* property) {
    network->getSettings()->save();
  }

 private:
  WProperty* onProperty;
  WProperty* volume;
  WProperty* btDeviceName;
  WProperty* numberOfStations;
  WProperty* editingTitle;
  WProperty* editingUrl;
  WProperty* station;
  WProperty* streamtitle;
  WAudio* radio;
  WBluetooth* bt;
  WM8978* dac;
  bool stopping = false;
  bool starting = false;

  void configureDevice() {
    byte nog = this->numberOfStations->getByte();
    this->numberOfStations->setByte(0);
    for (byte i = 0; i < nog; i++) {
      this->addStationConfig();
      this->numberOfStations->setByte(i + 1);
    }
    this->station->addEnumString(SOURCE_BLUETOOTH);
    for (byte i = 0; i < this->numberOfStations->getByte(); i++) {
      WProperty* stationTitle = getStationTitle(i);
      this->station->addEnumString(stationTitle->c_str());
      getStationUrl(i);
    }
    // Set station to 0, if needed
    byte i = this->station->getEnumIndex();
    if ((this->numberOfStations->getByte() > 0) &&
        ((i < 0) || (i >= this->numberOfStations->getByte()))) {
      this->station->setString(getStationTitle(0)->c_str());
    }
  }

  void clearEditingStation() {
    this->editingTitle = nullptr;
    this->editingUrl = nullptr;
  }

  void addStationConfigItem(String oTitle, String oUrl) {
    ensureEditingStation();
    network->debug(F("save station %s: / url: %s"), oTitle, oUrl);
    this->editingTitle->setString(oTitle.c_str());
    this->editingUrl->setString(oUrl.c_str());
    clearEditingStation();
  }
};

#endif
