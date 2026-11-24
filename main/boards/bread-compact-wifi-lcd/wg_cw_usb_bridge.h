#pragma once
// WG_CW_USB_LINK_V1: USB serial/JTAG CDC event bridge.
// Hardware: native USB D-/D+ GPIO19/20. Not intended for UART/COM connector.
// CW timer only queues small events. USB I/O is confined to FreeRTOS worker task.
#include <cstdint>
#include <cstdio>
#include <cstring>
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

    void Mode(bool cw) { Send(Event{2, cw ? 1 : 0, 0, esp_timer_get_time()}); }
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
            if (xQueueReceive(queue_, &e, pdMS_TO_TICKS(100)) == pdTRUE) {
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
            const int64_t now = esp_timer_get_time();
            if (now - last_hello > 2000000LL) {
                last_hello = now;
                Write("WGKEY:{\"type\":\"hello\",\"device\":\"WG-CW-ESP32S3\",\"protocol\":1,\"transport\":\"usb\"}\n");
            }
        }
    }
};
