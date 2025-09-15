#ifndef W_BLUETOOTH_H
#define W_BLUETOOTH_H

#include <Arduino.h>

#include "driver/i2s.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"

class WBluetooth;
WBluetooth* btAudio;

#define APP_SIG_WORK_DISPATCH (0x01)

typedef void (*app_callback_t)(uint16_t event, void* param);

typedef struct {
  uint16_t sig;      /*!< signal to app_task */
  uint16_t event;    /*!< message event id */
  app_callback_t cb; /*!< context switch callback */
  void* param;       /*!< parameter area needs to be last */
} app_msg_t;

enum {
  BT_APP_EVT_STACK_UP = 0,
};

enum A2dState {
  APP_AV_STATE_DISCONNECTED,
  APP_AV_STATE_CONNECTING,
  APP_AV_STATE_CONNECTED,
  APP_AV_STATE_DISCONNECTING
};
static const char* a2dStateString[4] = {"Disconnected", "Connecting",
                                        "Connected", "Disconnecting"};

enum A2dMediaState {
  APP_AV_MEDIA_STATE_SUSPENDED,
  APP_AV_MEDIA_STATE_STOPPED,
  APP_AV_MEDIA_STATE_STARTED
};
const char* a2dMediaStateString[3] = {"Suspended", "Stopped", "Started"};

void bt_task_handler(void* arg);
void bt_av_stack_handler(uint16_t event, void* p_param);
void bt_avrc_handler(esp_avrc_ct_cb_event_t event,
                     esp_avrc_ct_cb_param_t* param);

void bt_av_hdl_a2d_evt(uint16_t event, void* p_param);
void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param);
void bt_app_a2d_data_cb(const uint8_t* data, uint32_t len);
bool bt_app_work_dispatch(app_callback_t p_cback, uint16_t event,
                          void* p_params, int param_len);
bool bt_app_send_msg(app_msg_t* msg);
void bt_app_gap_callback(esp_bt_gap_cb_event_t event,
                         esp_bt_gap_cb_param_t* param);

typedef std::function<void(void)> TOnStateChange;

class WBluetooth {
 public:
  WBluetooth(const char* btSinkName) {
    btAudio = this;
    this->btSinkName = btSinkName;
  }

  ~WBluetooth() {
    esp_err_t res;
    log_i("Disconnect BT...");
    res = this->disconnect();
    if (res != ESP_OK) {
      log_e("Failed to disconnect a2d sink");
      return;
    }
    log_i("Shutdown BT task...");
    this->taskShutdown();
    btAudio = nullptr;
    delay(100);
    log_i("Deinit avrc...");
    esp_avrc_ct_deinit();
    delay(100);
    log_i("Deinit a2d sink...");
    res = esp_a2d_sink_deinit();
    if (res != ESP_OK) {
      log_e("Failed to deinit a2d sink");
      return;
    }
    delay(100);
    log_i("Disable Bluetooth...");
    res = esp_bluedroid_disable();
    if (res != ESP_OK) {
      log_e("Failed to disable bluedroid");
      return;
    }
    delay(100);
    log_i("Deinit Bluetooth...");
    res = esp_bluedroid_deinit();
    if (res != ESP_OK) {
      log_e("Failed to deinit bluedroid");
      return;
    }
    delay(100);
    log_i("Uninstall i2s driver...");
    // i2s_stop(m_i2s_num);
    res = i2s_driver_uninstall(m_i2s_num);
    if (res != ESP_OK) {
      log_e("Failed to uninstall i2s");
      return;
    }
    if(vBuffer) free(vBuffer);
    log_i("Bluetooth switched off.");
  }

  void setOnChange(TOnStateChange onChange) { this->onChange = onChange; }

  bool init(byte i2sBck, byte i2sWs, byte i2sDout, byte i2sClk) {
    esp_err_t res;
    if (!btStart()) {
      log_e("Failed to initialize controller");
      return false;
    } else
      log_i("controller initialized");

    res = esp_bluedroid_init();
    if (res != ESP_OK) {
      log_e("Failed to initialize bluedroid");
      return false;
    } else
      log_i("bluedroid initialized");

    res = esp_bluedroid_enable();
    if (res != ESP_OK) {
      log_e("Failed to enable bluedroid");
      return false;
    } else {
      log_i("bluedroid enabled");
    }

    // create application task
    this->taskStart();
    bt_app_work_dispatch(bt_av_stack_handler, BT_APP_EVT_STACK_UP, NULL, 0);
    esp_bt_gap_register_callback(bt_app_gap_callback);
    // i2s configuration
    m_i2s_config.sample_rate = 44100;
    m_i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    m_i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    m_i2s_config.intr_alloc_flags =
        ESP_INTR_FLAG_LEVEL1;  // default interrupt priority
    m_i2s_config.dma_buf_count = 8;
    m_i2s_config.dma_buf_len = 1024;
    m_i2s_config.use_apll = 0;
    m_i2s_config.tx_desc_auto_clear = true;
    m_i2s_config.fixed_mclk = I2S_PIN_NO_CHANGE;
    m_i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
#if ESP_ARDUINO_VERSION_MAJOR >= 2
    m_i2s_config.communication_format =
        (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S);  // Arduino vers.
                                                         // > 2.0.0
#else
    m_i2s_config.communication_format =
        (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
#endif
    i2s_driver_install(m_i2s_num, &m_i2s_config, 0, NULL);
    this->setPinout(i2sBck, i2sWs, i2sDout, i2sClk);
    return true;
  }

  bool isConnected() { return this->a2dState == APP_AV_STATE_CONNECTED; }

  esp_err_t disconnect() {
    if (remoteBda) {
      return esp_a2d_sink_disconnect(remoteBda);
    } else {
      return ESP_OK;
    }
  }

  void setVolume(uint8_t vol) {
    if (vol > 21) vol = 21;
    m_vol = volumetable[vol];
  }
  //---------------------------------------------------------------------------------------------------------------------
  uint8_t getVolume() {
    for (uint8_t i = 0; i < 22; i++) {
      if (volumetable[i] == m_vol) return i;
    }
    m_vol = 12;
    return m_vol;
  }

  void play() { execute_avrc_command(ESP_AVRC_PT_CMD_PLAY); }

  void pause() { execute_avrc_command(ESP_AVRC_PT_CMD_PAUSE); }

  void stop() { execute_avrc_command(ESP_AVRC_PT_CMD_STOP); }

  void next() { execute_avrc_command(ESP_AVRC_PT_CMD_FORWARD); }
  void previous() { execute_avrc_command(ESP_AVRC_PT_CMD_BACKWARD); }
  void fast_forward() { execute_avrc_command(ESP_AVRC_PT_CMD_FAST_FORWARD); }
  void rewind() { execute_avrc_command(ESP_AVRC_PT_CMD_REWIND); }

  void btTaskHandler(app_msg_t* msg) {
    log_d("event 0x%x, sig 0x%x", msg->event, msg->sig);
    if (msg->cb) {
      msg->cb(msg->event, msg->param);
    }
  }

  void btAppCallback(esp_bt_gap_cb_event_t event,
                     esp_bt_gap_cb_param_t* param) {
    switch (event) {
      case ESP_BT_GAP_READ_REMOTE_NAME_EVT: {
        log_d("ESP_BT_GAP_READ_REMOTE_NAME_EVT stat:%d",
              param->read_rmt_name.stat);
        if (param->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS) {
          log_d("ESP_BT_GAP_READ_REMOTE_NAME_EVT remote name:%s",
                param->read_rmt_name.rmt_name);
          this->setRemoteName((char*)param->read_rmt_name.rmt_name);
          // memcpy(remote_name, param->read_rmt_name.rmt_name,
          // ESP_BT_GAP_MAX_BDNAME_LEN );
        }
      } break;

      default: {
        log_d("Unhandled event: %d", event);
        break;
      }
    }
    return;
  }

  void updateStreamTitle(uint8_t attrId, const char* attrText) {
    switch (attrId) {
      case ESP_AVRC_MD_ATTR_TITLE: {
        this->streamtitle = String(attrText);
        break;
      }
      case ESP_AVRC_MD_ATTR_ARTIST: {
        String r = String(attrText);
        if (!r.equals("")) {
          r.concat(" - ");
        }
        r.concat(this->streamtitle);
        this->streamtitle = r;
        break;
      }
      default: {
        this->streamtitle = "";
      }
    }
    if (onChange) {
      onChange();
    }
  }

  String getStreamTitle() { return this->streamtitle; }

  esp_a2d_audio_state_t getMediaState() { return this->mediaState; }

  bool isPlaying() {
    return (this->mediaState ==
            (esp_a2d_audio_state_t)APP_AV_MEDIA_STATE_STARTED);
  }

  void setMediaState(esp_a2d_audio_state_t mediaState) {
    if (this->mediaState != mediaState) {
      this->mediaState = mediaState;
      if (onChange) {
        onChange();
      }
    }
  }

  String getRemoteName() { return this->remoteName; }

  void setRemoteName(const char* remoteName) {
    this->remoteName = String(remoteName);
    if (onChange) {
      onChange();
    }
  }

  void gainVolume() {
    if ((vBuffer) && (m_vol < 64)) {
      float step = (float) m_vol / 64;
      for (int i = 0; i < vBufferSize; i++) {      
        vBuffer[i] = vBuffer[i] * step;
      }  
    }
  }

  void bt_app_a2d_data_cb(const uint8_t* data, uint32_t len) {
    if ((!vBuffer) || (this->vBufferSize != len * 2)) {
      if(vBuffer) free(vBuffer);
      vBufferSize = len / 2;
      vBuffer = (int16_t*) malloc(len);
    }
    memcpy(vBuffer, data, len);
    gainVolume();
    size_t i2s_bytes_written;
    if (i2s_write(btAudio->m_i2s_num, /*(const char*) &s32*/ (void*) vBuffer, len,
                  &i2s_bytes_written, portMAX_DELAY) != ESP_OK) {
      log_e("i2s_write has failed");
    }

    if (i2s_bytes_written < len) {
      log_e("Timeout: not all bytes were written to I2S");
    }
  }

  const char* btSinkName;
  QueueHandle_t queueHandle = nullptr;
  TaskHandle_t taskHandle = nullptr;
  esp_a2d_mct_t s_audio_type = 0;
  i2s_config_t m_i2s_config = {};
  i2s_port_t m_i2s_num = I2S_NUM_0;
  i2s_pin_config_t s_pin_config;
  uint8_t* remoteBda = nullptr;
  int a2dState = APP_AV_STATE_DISCONNECTED;

 protected:
  void taskStart() {
    queueHandle = xQueueCreate(10, sizeof(app_msg_t));
    xTaskCreatePinnedToCore(bt_task_handler, "BtAppT", 4096, NULL,
                            (2 | portPRIVILEGE_BIT), &taskHandle,
                            tskNO_AFFINITY);
  }

  void taskShutdown(void) {
    if (taskHandle != nullptr) {
      vTaskDelete(taskHandle);
      taskHandle = nullptr;
    }
    if (queueHandle != nullptr) {
      vQueueDelete(queueHandle);
      queueHandle = nullptr;
    }
  }

  void execute_avrc_command(int cmd) {
    log_d("execute_avrc_command: 0x%x", cmd);
    esp_err_t ok =
        esp_avrc_ct_send_passthrough_cmd(0, cmd, ESP_AVRC_PT_CMD_STATE_PRESSED);
    if (ok == ESP_OK) {
      delay(100);
      ok = esp_avrc_ct_send_passthrough_cmd(0, cmd,
                                            ESP_AVRC_PT_CMD_STATE_RELEASED);
      if (ok == ESP_OK) {
        log_d("execute_avrc_command: %d -> OK", cmd);
      } else {
        log_e("execute_avrc_command ESP_AVRC_PT_CMD_STATE_PRESSED FAILED: %d",
              ok);
      }
    } else {
      log_e("execute_avrc_command ESP_AVRC_PT_CMD_STATE_RELEASED FAILED: %d",
            ok);
    }
  }

  bool setPinout(int8_t BCLK, int8_t LRC, int8_t DOUT,
                 int8_t MCK) {  // overwrite default pins

    s_pin_config.bck_io_num = BCLK;                // BCLK
    s_pin_config.ws_io_num = LRC;                  // LRC
    s_pin_config.data_out_num = DOUT;              // DOUT
    s_pin_config.data_in_num = I2S_PIN_NO_CHANGE;  // DIN
#if (ESP_IDF_VERSION_MAJOR >= 4 && ESP_IDF_VERSION_MINOR >= 4)
    s_pin_config.mck_io_num = MCK;
#endif

    const esp_err_t result = i2s_set_pin(m_i2s_num, &s_pin_config);
    return (result == ESP_OK);
  }

 private:
  String streamtitle;
  String remoteName;
  esp_a2d_audio_state_t mediaState = ESP_A2D_AUDIO_STATE_STOPPED;
  TOnStateChange onChange;
  const uint8_t volumetable[22] = {0,  1,  2,  3,  4,  6,  8,  10, 12, 14, 17,
                                   20, 23, 27, 30, 34, 38, 43, 48, 52, 58, 64};
  uint8_t m_vol = 64;
  int16_t* vBuffer = NULL;
  size_t vBufferSize = 0;
};

void bt_task_handler(void* arg) {
  app_msg_t msg;
  while (btAudio != nullptr) {
    if (pdTRUE == xQueueReceive(btAudio->queueHandle, &msg,
                                (portTickType)portMAX_DELAY)) {
      switch (msg.sig) {
        case APP_SIG_WORK_DISPATCH:
          log_d("APP_SIG_WORK_DISPATCH sig: %d", msg.sig);
          btAudio->btTaskHandler(&msg);
          break;
        default:
          log_e("Unhandled sig: 0%x", msg.sig);
          break;
      }
      if (msg.param) {
        free(msg.param);
      }
    }
  }
}

void bt_av_stack_handler(uint16_t event, void* p_param) {
  if (btAudio == nullptr) {
    return;
  }
  switch (event) {
    case BT_APP_EVT_STACK_UP: {
      log_d("av_hdl_stack_evt %s", "BT_APP_EVT_STACK_UP");
      /* set up device name */
      esp_bt_dev_set_device_name(btAudio->btSinkName);

      /* initialize AVRCP controller */
      esp_avrc_ct_init();
      esp_avrc_ct_register_callback(bt_avrc_handler);

      /* initialize A2DP sink */
      esp_a2d_register_callback(bt_app_a2d_cb);
      esp_a2d_sink_register_data_callback(bt_app_a2d_data_cb);
      esp_a2d_sink_init();

      /* set discoverable and connectable mode, wait to be connected */
      esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
      break;
    }
    default:
      log_e("unhandled evt %d", event);
      break;
  }
}

void bt_av_hdl_a2d_evt(uint16_t event, void* p_param) {
  if (btAudio == nullptr) {
    return;
  }
  esp_a2d_cb_param_t* a2d = NULL;
  switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT: {
      log_d("ESP_A2D_CONNECTION_STATE_EVT %i", event);
      a2d = (esp_a2d_cb_param_t*)(p_param);
      btAudio->a2dState = a2d->conn_stat.state;
      uint8_t* bda = a2d->conn_stat.remote_bda;
      esp_bt_gap_read_remote_name(a2d->conn_stat.remote_bda);
      log_i("A2DP connection state: %s %d, [%02x:%02x:%02x:%02x:%02x:%02x]",
            a2dStateString[btAudio->a2dState], btAudio->isConnected(), bda[0],
            bda[1], bda[2], bda[3], bda[4], bda[5]);
      break;
    }
    case ESP_A2D_AUDIO_STATE_EVT: {
      log_d("ESP_A2D_AUDIO_STATE_EVT %i", event);
      a2d = (esp_a2d_cb_param_t*)(p_param);
      btAudio->setMediaState(a2d->audio_stat.state);
      log_i("A2DP audio state: %s",
            a2dMediaStateString[btAudio->getMediaState()]);
      break;
    }
    case ESP_A2D_AUDIO_CFG_EVT: {
      log_d("ESP_A2D_AUDIO_CFG_EVT %i", event);
      esp_a2d_cb_param_t* esp_a2d_callback_param =
          (esp_a2d_cb_param_t*)(p_param);
      btAudio->s_audio_type = esp_a2d_callback_param->audio_cfg.mcc.type;
      a2d = (esp_a2d_cb_param_t*)(p_param);
      log_i("a2dp audio_cfg, codec type %d", a2d->audio_cfg.mcc.type);
      // for now only SBC stream is supported
      if (a2d->audio_cfg.mcc.type == ESP_A2D_MCT_SBC) {
        btAudio->m_i2s_config.sample_rate = 16000;
        char oct0 = a2d->audio_cfg.mcc.cie.sbc[0];
        if (oct0 & (0x01 << 6)) {
          btAudio->m_i2s_config.sample_rate = 32000;
        } else if (oct0 & (0x01 << 5)) {
          btAudio->m_i2s_config.sample_rate = 44100;
        } else if (oct0 & (0x01 << 4)) {
          btAudio->m_i2s_config.sample_rate = 48000;
        }

        i2s_set_clk(btAudio->m_i2s_num, btAudio->m_i2s_config.sample_rate,
                    btAudio->m_i2s_config.bits_per_sample, (i2s_channel_t)2);

        log_i("configure audio player [%02x-%02x-%02x-%02x]",
              a2d->audio_cfg.mcc.cie.sbc[0], a2d->audio_cfg.mcc.cie.sbc[1],
              a2d->audio_cfg.mcc.cie.sbc[2], a2d->audio_cfg.mcc.cie.sbc[3]);
        log_i("audio player configured, samplerate=%d",
              btAudio->m_i2s_config.sample_rate);
      }
      break;
    }
#if (ESP_IDF_VERSION_MAJOR >= 4)
    case ESP_A2D_PROF_STATE_EVT: {
      a2d = (esp_a2d_cb_param_t*)(p_param);
      if (ESP_A2D_INIT_SUCCESS == a2d->a2d_prof_stat.init_state) {
        log_i("A2DP PROF STATE: Init Compl\n");
      } else {
        log_i("A2DP PROF STATE: Deinit Compl\n");
      }
    } break;
#endif
    default:
      log_e("unhandled evt 0x%x", event);
      break;
  }
}

void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param) {
  if (btAudio == nullptr) {
    return;
  }
  switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
      log_d("ESP_A2D_CONNECTION_STATE_EVT");
      bt_app_work_dispatch(bt_av_hdl_a2d_evt, event, param,
                           sizeof(esp_a2d_cb_param_t));
      break;
    case ESP_A2D_AUDIO_STATE_EVT:
      log_d("ESP_A2D_AUDIO_STATE_EVT");
      btAudio->setMediaState(param->audio_stat.state);
      bt_app_work_dispatch(bt_av_hdl_a2d_evt, event, param,
                           sizeof(esp_a2d_cb_param_t));
      break;
    case ESP_A2D_AUDIO_CFG_EVT: {
      log_d("ESP_A2D_AUDIO_CFG_EVT");
      bt_app_work_dispatch(bt_av_hdl_a2d_evt, event, param,
                           sizeof(esp_a2d_cb_param_t));
      break;
    }
#if (ESP_IDF_VERSION_MAJOR >= 4)
    case ESP_A2D_PROF_STATE_EVT: {
      log_d("ESP_A2D_PROF_STATE_EVT");
      bt_app_work_dispatch(bt_av_hdl_a2d_evt, event, param,
                           sizeof(esp_a2d_cb_param_t));
      break;
    }
#endif
    default:
      log_e("Invalid A2DP event: %d");
      break;
  }
}

void bt_app_a2d_data_cb(const uint8_t* data, uint32_t len) {
  if (btAudio == nullptr) {
    return;
  }
  btAudio->bt_app_a2d_data_cb(data, len);
}

void bt_app_alloc_meta_buffer(esp_avrc_ct_cb_param_t* param) {
  esp_avrc_ct_cb_param_t* rc = (esp_avrc_ct_cb_param_t*)(param);
  uint8_t* attr_text = (uint8_t*)malloc(rc->meta_rsp.attr_length + 1);
  memcpy(attr_text, rc->meta_rsp.attr_text, rc->meta_rsp.attr_length);
  attr_text[rc->meta_rsp.attr_length] = 0;
  log_d("attr_text= %s", attr_text);
  rc->meta_rsp.attr_text = attr_text;
}

void bt_av_new_track() {
  // Register notifications and request metadata
  esp_avrc_ct_send_metadata_cmd(
      0, ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST);
  esp_avrc_ct_send_register_notification_cmd(1, ESP_AVRC_RN_TRACK_CHANGE, 0);
}

void bt_av_notify_evt_handler(uint8_t event_id) {
  switch (event_id) {
    case ESP_AVRC_RN_TRACK_CHANGE:
      log_d("ESP_AVRC_RN_TRACK_CHANGE %d", event_id);
      bt_av_new_track();
      break;
    default:
      log_e("Unhandled av notify evt %d", event_id);
      break;
  }
}

void bt_av_hdl_avrc_evt(uint16_t event, void* p_param) {
  if (btAudio == nullptr) {
    return;
  }
  esp_avrc_ct_cb_param_t* rc = (esp_avrc_ct_cb_param_t*)(p_param);
  switch (event) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT: {
      uint8_t* bda = rc->conn_stat.remote_bda;
      btAudio->remoteBda = rc->conn_stat.remote_bda;
      log_i("AVRC conn_state evt: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",
            rc->conn_stat.connected, bda[0], bda[1], bda[2], bda[3], bda[4],
            bda[5]);

      if (rc->conn_stat.connected) {
        bt_av_new_track();
      }
      break;
    }
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT: {
      log_i("AVRC passthrough rsp: key_code 0x%x, key_state %d",
            rc->psth_rsp.key_code, rc->psth_rsp.key_state);
      break;
    }
    case ESP_AVRC_CT_METADATA_RSP_EVT: {
      log_i("AVRC metadata rsp: attribute id 0x%x, %s", rc->meta_rsp.attr_id,
            rc->meta_rsp.attr_text);
      btAudio->updateStreamTitle(rc->meta_rsp.attr_id,
                                 (char*)rc->meta_rsp.attr_text);
      free(rc->meta_rsp.attr_text);
      break;
    }
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT: {
      log_i("AVRC event notification: %d, param: %d", rc->change_ntf.event_id,
            rc->change_ntf.event_parameter);
      bt_av_notify_evt_handler(rc->change_ntf.event_id);
      break;
    }
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT: {
      log_i("AVRC remote features 0x%x", rc->rmt_feats.feat_mask);
      break;
    }
    default:
      log_e("Unhandled av evt %d", event);
      break;
  }
}

void bt_avrc_handler(esp_avrc_ct_cb_event_t event,
                     esp_avrc_ct_cb_param_t* param) {
  switch (event) {
    case ESP_AVRC_CT_METADATA_RSP_EVT:
      log_d("ESP_AVRC_CT_METADATA_RSP_EVT");
      bt_app_alloc_meta_buffer(param);
      bt_app_work_dispatch(bt_av_hdl_avrc_evt, event, param,
                           sizeof(esp_avrc_ct_cb_param_t));
      break;
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
      log_d("ESP_AVRC_CT_CONNECTION_STATE_EVT");
      bt_app_work_dispatch(bt_av_hdl_avrc_evt, event, param,
                           sizeof(esp_avrc_ct_cb_param_t));
      break;
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
      log_d("ESP_AVRC_CT_PASSTHROUGH_RSP_EVT");
      bt_app_work_dispatch(bt_av_hdl_avrc_evt, event, param,
                           sizeof(esp_avrc_ct_cb_param_t));
      break;
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
      log_d("ESP_AVRC_CT_CHANGE_NOTIFY_EVT");
      bt_app_work_dispatch(bt_av_hdl_avrc_evt, event, param,
                           sizeof(esp_avrc_ct_cb_param_t));
      break;
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT: {
      log_d("ESP_AVRC_CT_REMOTE_FEATURES_EVT");
      bt_app_work_dispatch(bt_av_hdl_avrc_evt, event, param,
                           sizeof(esp_avrc_ct_cb_param_t));
      break;
    }
    default:
      log_e("Unhandled AVRC event: %d", event);
      break;
  }
}

bool bt_app_work_dispatch(app_callback_t p_cback, uint16_t event,
                          void* p_params, int param_len) {
  log_d("event 0x%x, param len %d", event, param_len);

  app_msg_t msg;
  memset(&msg, 0, sizeof(app_msg_t));

  msg.sig = APP_SIG_WORK_DISPATCH;
  msg.event = event;
  msg.cb = p_cback;

  if (param_len == 0) {
    return bt_app_send_msg(&msg);
  } else if (p_params && param_len > 0) {
    if ((msg.param = malloc(param_len)) != NULL) {
      memcpy(msg.param, p_params, param_len);
      return bt_app_send_msg(&msg);
    }
  }
  return false;
}

bool bt_app_send_msg(app_msg_t* msg) {
  if (btAudio == nullptr) {
    return false;
  }
  log_d("event 0x%x, sig 0x%x", msg->event, msg->sig);
  if ((btAudio->queueHandle == nullptr) || (msg == NULL)) {
    return false;
  }

  if (xQueueSend(btAudio->queueHandle, msg, 10 / portTICK_RATE_MS) != pdTRUE) {
    log_e("xQueue send failed");
    return false;
  }
  return true;
}

void bt_app_gap_callback(esp_bt_gap_cb_event_t event,
                         esp_bt_gap_cb_param_t* param) {
  if (btAudio) btAudio->btAppCallback(event, param);
}

#endif
