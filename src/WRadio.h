#ifndef W_RADIO_H
#define W_RADIO_H

#include <Arduino.h>

#include "WAudio.h"
#include "WDevice.h"
//#include "WM8978.h"
#include "WNetwork.h"
#include "hw/WSwitch.h"

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
const static char* POWER_OFF = "<off>";

// T-Audio 1.6 WM8978 I2C pins.
//#define WM8978_I2C_SDA 19
//#define WM8978_I2C_SCL 18
// T-Audio 1.6 WM8978 I2S pins.
#define WM8978_I2S_BCK 25
#define WM8978_I2S_LRC 27
#define WM8978_I2S_DOUT 26
#define XSMT 15
//#define WM8978_I2S_DIN 27
// T-Audio 1.6 WM8978 MCLK gpio number?
//#define WM8978_I2S_MCLKPIN GPIO_NUM_0

// #external DAC
// #define I2S_LRCK 25
// #define I2S_DIN 26
// #define I2S_BCLK 27

struct WRadioStation {
  WValue* url = nullptr;

  WRadioStation(const char* url) {
    this->url = new WValue(url);
  }

  virtual ~WRadioStation() {
    if (url) delete url;
  }
};

class WRadio : public WDevice {
 public:
  WRadio(WNetwork* network)
      : WDevice(network, DEVICE_ID, network->getIdx(), DEVICE_TYPE_RADIO,
                DEVICE_TYPE_ON_OFF_SWITCH) {

    pinMode(XSMT, OUTPUT);
    
    

    this->radio = nullptr;
    /*this->dac = new WM8978();
    // Setup wm8978 I2C interface
    if (dac->begin(WM8978_I2C_SDA, WM8978_I2C_SCL)) {
      LOG->debug("DAC ok");
    } else {
      LOG->debug("Setting up DAC failed.");
    }*/
    // On
    this->onProperty = WProps::createOnOffProperty("Power");
    this->onProperty->asBool(false);
    SETTINGS->add(this->onProperty->value());
    this->onProperty->addListener(std::bind(&WRadio::onPropertyChanged, this));
    this->addProperty(onProperty, "on");
    // Volume
    this->volume = WProps::createLevelIntProperty("Volume", 0, 21);
    this->volume->asInt(21);
    SETTINGS->add(this->volume->value());
    this->volume->addListener(
        std::bind(&WRadio::volumeChanged, this));
    this->addProperty(this->volume, "volume");
    //this->dac->setHPvol(63, 63);
    // Streamtext
    this->streamtitle = WProps::createStringProperty("Title");
    this->addProperty(this->streamtitle, "streamtitle");
    // Station Memory
    this->editingTitle = nullptr;
    this->editingUrl = nullptr;
    this->numberOfStations = WProps::createByteProperty()->asByte(MAX_STATIONS);
    SETTINGS->add(this->numberOfStations->value());
    this->numberOfStations->visibility(NONE);
    this->addProperty(this->numberOfStations, "numberOfStations");
    this->station = WProps::createStringProperty();
    SETTINGS->add(this->station->value());
    this->addProperty(this->station, "station");
    // Configure Device
    configureDevice();
    this->station->addListener(std::bind(&WRadio::stationChanged, this));

    // HtmlPages
    /*WPage* configPage = new WPage(this->id(), "Configure radio");
    //configPage->setPrintPage(std::bind(&WRadio::printConfigPage, this, std::placeholders::_1, std::placeholders::_2));
    //configPage->setSubmittedPage(std::bind(&WRadio::saveConfigPage, this, std::placeholders::_1, std::placeholders::_2));
    network->addCustomPage(configPage);
    // Add/Edit station
    WPage* stationPage = new WPage(HTTP_EDIT_STATION, "Add/edit station");
    stationPage->setShowInMainMenu(false);
    stationPage->setPrintPage(std::bind(&WRadio::handleHttpEditStation, this, std::placeholders::_1, std::placeholders::_2));
    stationPage->setSubmittedPage(std::bind(&WRadio::handleHttpSubmitEditStation, this, std::placeholders::_1, std::placeholders::_2));
    stationPage->setTargetAfterSubmitting(configPage);
    network->addCustomPage(stationPage);
    // Remove station
    WPage* removeStation = new WPage(HTTP_REMOVE_STATION, "Remove station");
    removeStation->setShowInMainMenu(false);
    removeStation->setPrintPage(std::bind(&WRadio::handleHttpRemoveStationButton, this, std::placeholders::_1, std::placeholders::_2));
    network->addCustomPage(removeStation);*/
  }

  void play() {
    if (!starting) {
      starting = true;
      if ((this->radio == nullptr) && (network()->isWifiConnected())) {
        this->streamtitle->asString(this->getStationTitle(station->enumIndex())->asString());
        log_i("Radio on");
        delay(100);        
        stop();
        this->radio = new WAudio();
        this->radio->setOnChange([this]() {
          this->streamtitle->asString(this->radio->getStreamTitle().c_str());
        });
        //this->radio->init(WM8978_I2S_BCK, WM8978_I2S_WS, WM8978_I2S_DOUT, WM8978_I2S_MCLKPIN);
        this->radio->init(WM8978_I2S_BCK, WM8978_I2S_LRC, WM8978_I2S_DOUT, I2S_PIN_NO_CHANGE);
        digitalWrite(XSMT, HIGH);
        if (!this->radio->play("http://stream.104.6rtl.com/rtl-live/mp3-192/play.m3u")) {  // this->getStationUrl(station->enumIndex())->asString())) {
          network()->debug(F("Can't connect to '%s'"), this->getStationUrl(station->enumIndex())->asString());
          stop();
        } else {
          if (!this->radio->isRunning()) {
            network()->debug(F("Radio not running, reason unknown"));
            stop();
          } else {
            this->radio->setVolume(0);
            unMute();
          }
        }
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
      stopping = false;
    }
  }

  void mute() {
    byte v = this->volume->asInt();
    if (this->radio != nullptr) {
      while (v > 0) {
        this->radio->setVolume(v);
        v--;
        delay(30);
      }
      this->radio->setVolume(0);
    }
  }

  void unMute() {
    byte v = 0;
    if (this->radio != nullptr) {
      while (v <= 21) {
        this->radio->setVolume(v);
        v++;
        delay(30);
      }
    }
  }

  void loop(unsigned long now) {
    WDevice::loop(now);
    if (onProperty->asBool()) {
      play();
    } else {
      stop();
    }
  }

  WProperty* getStreamTitle() { return this->streamtitle; }

  WList<WRadioStation>* stations() { return _stations; }

  /*void handleHttpRemoveStationButton(AsyncWebServerRequest* request,
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
    WHtml::table(page, "st");
    WHtml::tr(page);
    WHtml::th(page);
    page->print("No.");
    WHtml::thEnd(page);
    WHtml::th(page);
    page->print("Name");
    WHtml::thEnd(page);
    WHtml::th(page);
    if (numberOfStations->asByte() < MAX_STATIONS)
      page->printf(HTTP_BUTTON, HTTP_EDIT_STATION, "get", CAPTION_ADD);
    WHtml::thEnd(page);
    WHtml::th(page);
    WHtml::thEnd(page);
    WHtml::trEnd(page);
    char* pNumber = new char[2];
    for (byte i = 0; i < this->numberOfStations->asByte(); i++) {
      WProperty* stationTitle = getStationTitle(i);
      sprintf(pNumber, "%d", i);
      WHtml::tr(page);
      WHtml::td(page);
      page->print(pNumber);
      WHtml::tdEnd(page);
      WHtml::td(page);
      page->print(stationTitle->c_str());
      WHtml::tdEnd(page);
      WHtml::td(page);
      page->printf(HTTP_BUTTON_VALUE, HTTP_EDIT_STATION, "", pNumber,
                   CAPTION_EDIT);
      WHtml::tdEnd(page);
      WHtml::td(page);
      page->printf(HTTP_BUTTON_VALUE, HTTP_REMOVE_STATION, "cbtn", pNumber,
                   CAPTION_REMOVE);
      WHtml::tdEnd(page);
      WHtml::trEnd(page);
    }
    page->print(F("</table>"));
    page->printf(HTTP_DIV_END);

    page->printf(HTTP_CONFIG_PAGE_BEGIN, id());
    page->printf(HTTP_TOGGLE_GROUP_STYLE, "ga", (this->btEnabled->asBool() ? HTTP_CHECKED : HTTP_NONE), "gb", HTTP_NONE);
    //BT enabled
    page->printf(HTTP_CHECKBOX_OPTION, "be", "be", (this->btEnabled->asBool() ? HTTP_CHECKED : ""), "tg()", "Enable Bluetooth receiver");
    page->printf(HTTP_DIV_ID_BEGIN, "ga");
    //BT receiver name
    page->printf(HTTP_TEXT_FIELD, "Bluetooth receiver name: ", "btn", "16", this->btDeviceName->c_str());
    page->print(FPSTR(HTTP_DIV_END));
    page->printf(HTTP_TOGGLE_FUNCTION_SCRIPT, "tg()", "be", "ga", "gb");
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

  void saveConfigPage(AsyncWebServerRequest* request, Print* page) {
    this->btEnabled->asBool(request->arg("be") == HTTP_TRUE);
    this->btDeviceName->asString(request->arg("btn").c_str());
    //network->getSettings()->save();
  }*/

 protected:
  WValue* getStationTitle(byte index) {
    char* pNumber = new char[6];
    sprintf(pNumber, DEFAULT_STATION_TITLE, index);
    return SETTINGS->getById(pNumber);
  }

  WValue* getStationUrl(byte index) {
    char* pNumber = new char[6];
    sprintf(pNumber, DEFAULT_STATION_URL, index);
    return SETTINGS->getById(pNumber);
  }

  WValue* addStationConfig() {
    byte index = this->numberOfStations->asByte();
    char* pNumber = new char[6];
    // Title
    sprintf(pNumber, DEFAULT_STATION_TITLE, index);
    WValue* station = SETTINGS->setString(pNumber, "");
    // Url
    sprintf(pNumber, DEFAULT_STATION_URL, index);
    SETTINGS->setString(pNumber, "");
    return station;
  }

  void removeStationConfig(byte index) {
    char* pNumber = new char[6];
    // Title
    sprintf(pNumber, DEFAULT_STATION_TITLE, index);
    SETTINGS->remove(pNumber);
    // Url
    sprintf(pNumber, DEFAULT_STATION_URL, index);
    SETTINGS->remove(pNumber);
    char* p2 = new char[6];
    for (int i = index + 1; i < this->numberOfStations->asByte(); i++) {
      // Title
      sprintf(pNumber, DEFAULT_STATION_TITLE, i);
      sprintf(p2, DEFAULT_STATION_TITLE, i - 1);
      // SETTINGS->getSetting(pNumber)->id(p2);
      //  Url
      sprintf(pNumber, DEFAULT_STATION_URL, i);
      sprintf(p2, DEFAULT_STATION_URL, i - 1);
      // SETTINGS->getSetting(pNumber)->id(p2);
    }
    this->numberOfStations->asByte(this->numberOfStations->asByte() - 1);
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
      this->editingUrl = getStationUrl(this->numberOfStations->asByte());
      this->numberOfStations->asByte(this->numberOfStations->asByte() + 1);
    }
  }

  void onPropertyChanged() {
    SETTINGS->save();
    if (!this->onProperty->asBool()) {
      this->streamtitle->asString(POWER_OFF);
    }
  }

  void stationChanged() {
    SETTINGS->save();
    delay(100);
    stop();
    this->streamtitle->asString(this->station->asString());
    this->play();
  }

  void volumeChanged() {
    SETTINGS->save();
    if (this->radio != nullptr) {
      this->radio->setVolume(volume->asInt());
    }
  }  

 private:
  WProperty* onProperty;
  WProperty* volume;
  WProperty* numberOfStations;
  WValue* editingTitle;
  WValue* editingUrl;
  WProperty* station;
  WProperty* streamtitle;
  WAudio* radio;
  //WM8978* dac;
  bool stopping = false;
  bool starting = false;
  WList<WRadioStation>* _stations = new WList<WRadioStation>();

  void configureDevice() {
    byte nog = this->numberOfStations->asByte();
    this->numberOfStations->asByte(0);
    for (byte i = 0; i < nog; i++) {
      this->addStationConfig();
      this->numberOfStations->asByte(i + 1);
    }
    for (byte i = 0; i < this->numberOfStations->asByte(); i++) {
      WValue* stationTitle = getStationTitle(i);
      this->station->addEnumString(stationTitle->asString());
      getStationUrl(i);
    }
    // Set station to 0, if needed
    byte i = this->station->enumIndex();
    if ((this->numberOfStations->asByte() > 0) &&
        ((i < 0) || (i >= this->numberOfStations->asByte()))) {
      this->station->asString(getStationTitle(0)->asString());
    }
  }

  void clearEditingStation() {
    this->editingTitle = nullptr;
    this->editingUrl = nullptr;
  }

  void addStationConfigItem(String oTitle, String oUrl) {
    ensureEditingStation();
    network()->debug(F("save station %s: / url: %s"), oTitle, oUrl);
    this->editingTitle->asString(oTitle.c_str());
    this->editingUrl->asString(oUrl.c_str());
    clearEditingStation();
  }
};

#endif
