#pragma once
// WG_CW_USB_LINK_V1: USB serial/JTAG CDC event bridge.
// Hardware: native USB D-/D+ GPIO19/20. Not intended for UART/COM connector.
// CW timer only queues small events. USB I/O is confined to FreeRTOS worker task.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "cJSON.h"
#include "application.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

class WgCwUsbBridge {
public:
    static WgCwUsbBridge& Get() {
        static WgCwUsbBridge instance;
        return instance;
    }

    void Start() {
        if (queue_) return;
        queue_ = xQueueCreate(48, sizeof(Event));
        if (!queue_) {
            ESP_LOGE("WG_CW_USB", "Cannot allocate event queue");
            return;
        }
        if (xTaskCreate(&WgCwUsbBridge::TaskEntry, "wg_cw_usb", 4096,
                        this, 3, nullptr) != pdPASS) {
            ESP_LOGE("WG_CW_USB", "Cannot create USB worker task");
            vQueueDelete(queue_);
            queue_ = nullptr;
        }
    }

    void Mode(bool cw) { Send(Event{2, static_cast<int8_t>(cw ? 1 : 0), 0, esp_timer_get_time()}); }
    void Down(int64_t now_us) { Send(Event{0, 0, 0, now_us}); }
    void Up(int64_t now_us, unsigned duration_ms) {
        Send(Event{1, 0, duration_ms, now_us});
    }

private:
    struct Event {
        uint8_t kind;   // 0=down, 1=up, 2=mode
        int8_t mode;
        uint32_t duration_ms;
        int64_t timestamp_us;
    };
    QueueHandle_t queue_ = nullptr;
    uint32_t seq_ = 0;
    bool mode_cw_ = false;

    void Send(const Event& e) {
        if (queue_) (void)xQueueSend(queue_, &e, 0);
    }
    static void TaskEntry(void* arg) { static_cast<WgCwUsbBridge*>(arg)->Run(); }
    void Write(const char* line) {
        // No blocking USB writes from esp_timer callbacks; allow small finite timeout.
        const int length = static_cast<int>(std::strlen(line));
        if (length > 0) {
            (void)usb_serial_jtag_write_bytes(line, length, pdMS_TO_TICKS(10));
        }
    }

    std::string rx_;
    bool discard_=false;
    unsigned last_finished_=0;
    void Command(const std::string& line) {
        if(line.compare(0,6,"WGCMD:")!=0) return;
        cJSON* j=cJSON_Parse(line.c_str()+6); if(!j)return;
        auto str=[&](const char* k)->const char*{auto* v=cJSON_GetObjectItemCaseSensitive(j,k);return cJSON_IsString(v)?v->valuestring:"";};
        auto num=[&](const char* k,int fallback,int lo,int hi){auto* v=cJSON_GetObjectItemCaseSensitive(j,k);int x=cJSON_IsNumber(v)?v->valueint:fallback;return x<lo?lo:x>hi?hi:x;};
        auto& app=Application::GetInstance(); auto& tr=app.GetAudioService().WgTraining();
        const char* type=str("type"); const char* state="rejected";
        if(mode_cw_ && app.IsWgCwMode()) {
            if(!strcmp(type,"config")||!strcmp(type,"play")) {
                tr.tone.store(num("tone",tr.tone.load(),300,1200));tr.volume.store(num("volume",tr.volume.load(),0,100));tr.wpm.store(num("wpm",tr.wpm.load(),5,72));
                if(!strcmp(type,"config")) state="configured";
                else if(tr.Start(str("codes"),num("noise",0,0,100),num("qrm",0,0,100),num("qsb",0,0,100),num("drift",0,0,100))) state="playing";
            } else if(!strcmp(type,"stop")) {tr.Stop();state="stopped";}
        }
        char reply[160]; snprintf(reply,sizeof(reply),"WGKEY:{\"type\":\"rx_status\",\"state\":\"%s\",\"id\":%d}\n",state,num("id",0,0,2147483647)); Write(reply);cJSON_Delete(j);
    }
    void ReadCommands() {
        uint8_t bytes[128]; int n=usb_serial_jtag_read_bytes(bytes,sizeof(bytes),0);
        for(int i=0;i<n;i++) {
            char c=static_cast<char>(bytes[i]);
            if(c=='\n') {if(!discard_)Command(rx_);rx_.clear();discard_=false;}
            else if(c!='\r'&&!discard_) {if(rx_.size()<4096)rx_+=c;else {rx_.clear();discard_=true;}}
        }
        auto& tr=Application::GetInstance().GetAudioService().WgTraining();
        unsigned f=tr.finished.load(); if(f!=last_finished_) {last_finished_=f;Write("WGKEY:{\"type\":\"rx_status\",\"state\":\"done\"}\n");}
    }

    void Run() {
        usb_serial_jtag_driver_config_t cfg = {};
        cfg.rx_buffer_size = 256;
        cfg.tx_buffer_size = 2048;
        esp_err_t result = usb_serial_jtag_driver_install(&cfg);
        // May already be installed if firmware owns USB Serial/JTAG as console.
        if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
            ESP_LOGE("WG_CW_USB", "USB Serial/JTAG driver: %s", esp_err_to_name(result));
            vTaskDelete(nullptr);
            return;
        }
        ESP_LOGI("WG_CW_USB", "USB key bridge ready; native USB port only");
        int64_t last_hello = 0;
        while (true) {
            Event e = {};
            if (xQueueReceive(queue_, &e, pdMS_TO_TICKS(10)) == pdTRUE) {
                char line[192] = {};
                if (e.kind == 2) {
                    mode_cw_ = (e.mode == 1);
                    std::snprintf(line, sizeof(line),
                                  "WGKEY:{\"type\":\"mode\",\"mode\":\"%s\"}\n",
                                  mode_cw_ ? "cw" : "xiaozhi");
                } else if (mode_cw_) {
                    ++seq_;
                    if (e.kind == 0) {
                        std::snprintf(line, sizeof(line),
                            "WGKEY:{\"type\":\"key_event\",\"state\":\"down\",\"seq\":%lu,\"t_us\":%lld}\n",
                            static_cast<unsigned long>(seq_), static_cast<long long>(e.timestamp_us));
                    } else {
                        std::snprintf(line, sizeof(line),
                            "WGKEY:{\"type\":\"key_event\",\"state\":\"up\",\"seq\":%lu,\"t_us\":%lld,\"durationMs\":%lu}\n",
                            static_cast<unsigned long>(seq_), static_cast<long long>(e.timestamp_us),
                            static_cast<unsigned long>(e.duration_ms));
                    }
                }
                if (line[0]) Write(line);
            }
            ReadCommands();
            const int64_t now = esp_timer_get_time();
            if (now - last_hello > 2000000LL) {
                last_hello = now;
                char hello[180]; snprintf(hello,sizeof(hello),"WGKEY:{\"type\":\"hello\",\"device\":\"WG-CW-ESP32S3\",\"protocol\":2,\"transport\":\"usb\",\"remote\":true,\"mode\":\"%s\"}\n",mode_cw_ ? "cw" : "xiaozhi"); Write(hello);
            }
        }
    }
};
