#pragma once
// WG-CW GPIO17 straight-key monitor, phase 1: serial events only.
// Audio, LCD, AI, and Wi-Fi modules are untouched.
#include <stdint.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class WgCwKeyMonitor {
public:
    static void Start(gpio_num_t pin) {
        gpio_config_t conf = {};
        conf.pin_bit_mask = 1ULL << static_cast<unsigned>(pin);
        conf.mode = GPIO_MODE_INPUT;
        conf.pull_up_en = GPIO_PULLUP_ENABLE;
        conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        conf.intr_type = GPIO_INTR_DISABLE;
        esp_err_t err = gpio_config(&conf);
        if (err != ESP_OK) {
            ESP_LOGE("WG_CW", "GPIO config failed: %d", static_cast<int>(err));
            return;
        }
        auto* param = new gpio_num_t(pin);
        BaseType_t ok = xTaskCreate(&Task, "wg_cw_key", 3072, param, 4, nullptr);
        if (ok != pdPASS) {
            delete param;
            ESP_LOGE("WG_CW", "Key monitor task failed");
        }
    }
private:
    static void Task(void* arg) {
        const gpio_num_t pin = *static_cast<gpio_num_t*>(arg);
        delete static_cast<gpio_num_t*>(arg);
        const int64_t debounce_us = 3000;
        bool stable_down = (gpio_get_level(pin) == 0);
        bool candidate_down = stable_down;
        int64_t candidate_since = esp_timer_get_time();
        int64_t down_time = 0;
        unsigned seq = 0;
        ESP_LOGI("WG_CW", "GPIO%d READY initial=%s", static_cast<int>(pin), stable_down ? "DOWN" : "UP");
        for (;;) {
            const int64_t now = esp_timer_get_time();
            bool sampled_down = (gpio_get_level(pin) == 0);
            if (sampled_down != candidate_down) {
                candidate_down = sampled_down;
                candidate_since = now;
            }
            if (candidate_down != stable_down && (now - candidate_since) >= debounce_us) {
                stable_down = candidate_down;
                // First stable edge timestamps are based on the start of the stable candidate.
                const int64_t edge_time = candidate_since;
                if (stable_down) {
                    ++seq;
                    down_time = edge_time;
                    ESP_LOGI("WG_CW", "KEY_DOWN seq=%u t_us=%lld", seq, (long long)edge_time);
                } else {
                    long long hold = (down_time > 0) ? (long long)(edge_time - down_time) : 0LL;
                    ESP_LOGI("WG_CW", "KEY_UP seq=%u t_us=%lld hold_us=%lld", seq,
                             (long long)edge_time, hold);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
};
