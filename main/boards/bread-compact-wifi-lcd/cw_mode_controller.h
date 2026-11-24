#pragma once

// WG-CW first hardware test for XiaoZhi v2.2.6 / bread-compact-wifi-lcd.
// GPIO39 hold 2 seconds: toggle AI/CW monitoring mode.
// GPIO17 to GND: CW straight key, active LOW, 30 ms debounce.
// This stage does not take over I2S/audio or modify the XiaoZhi voice pipeline.

#include <cstdint>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include "application.h"
#include "board.h"
#include "wg_cw_decoder.h" // WG_CW_LIVE_UI_V1

class WgCwModeController {
public:
    bool IsCwMode() const { return cw_mode_; }

    void Start() {
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = (1ULL << 17) | (1ULL << 39);
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = GPIO_PULLUP_ENABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&cfg));

        const int64_t now = esp_timer_get_time();
        InitKey(cw_key_, GPIO_NUM_17, now);
        InitKey(volume_down_, GPIO_NUM_39, now);

        esp_timer_create_args_t args = {};
        args.callback = &WgCwModeController::TimerThunk;
        args.arg = this;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "wg_cw_keys";
        args.skip_unhandled_events = true;
        ESP_ERROR_CHECK(esp_timer_create(&args, &timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(timer_, 10000)); // 10 ms
        ESP_LOGI("WG_CW", "Ready: GPIO17 key, GPIO39 hold 2s to toggle mode");
    }

private:
    struct Key {
        gpio_num_t pin = GPIO_NUM_NC;
        bool raw_pressed = false;
        bool stable_pressed = false;
        int64_t raw_changed_us = 0;
    };

    static constexpr int64_t kDebounceUs = 30000;
    static constexpr int64_t kComboHoldUs = 2000000;

    esp_timer_handle_t timer_ = nullptr;
    Key cw_key_;
    Key volume_down_;
    bool cw_mode_ = false;
    bool combo_latched_ = false;
    int64_t combo_started_us_ = 0;
    int64_t cw_pressed_us_ = 0;
    WgCwDecoder cw_decoder_; // WG_CW_LIVE_UI_V1

    void EmitCwSnapshot() {
        const auto snapshot = cw_decoder_.Snapshot();
        Application::GetInstance().Schedule([snapshot]() {
            WgCwLiveUi::Refresh(snapshot);
        });
    }


    static void TimerThunk(void* ctx) {
        static_cast<WgCwModeController*>(ctx)->Poll();
    }

    static void InitKey(Key& key, gpio_num_t pin, int64_t now) {
        key.pin = pin;
        key.raw_pressed = gpio_get_level(pin) == 0;
        key.stable_pressed = key.raw_pressed;
        key.raw_changed_us = now;
    }

    static bool UpdateKey(Key& key, int64_t now) {
        bool raw = gpio_get_level(key.pin) == 0;
        if (raw != key.raw_pressed) {
            key.raw_pressed = raw;
            key.raw_changed_us = now;
        }
        if (raw != key.stable_pressed && now - key.raw_changed_us >= kDebounceUs) {
            key.stable_pressed = raw;
            return true;
        }
        return false;
    }

    void Poll() {
        const int64_t now = esp_timer_get_time();
        const bool cw_changed = UpdateKey(cw_key_, now);
        UpdateKey(volume_down_, now);

        const bool button_held = volume_down_.stable_pressed;
        if (!button_held) {
            combo_started_us_ = 0;
            combo_latched_ = false;
        } else if (combo_started_us_ == 0) {
            combo_started_us_ = now;
        } else if (!combo_latched_ && now - combo_started_us_ >= kComboHoldUs) {
            combo_latched_ = true;
            cw_mode_ = !cw_mode_;
            cw_pressed_us_ = 0;
            cw_decoder_.Reset(); // WG_CW_LIVE_UI_V1
            const bool entering_cw = cw_mode_;
            ESP_LOGI("WG_CW", "MODE=%s", entering_cw ? "CW" : "XIAOZHI");
            // Any UI/audio calls must be scheduled to XiaoZhi main task.
            Application::GetInstance().Schedule([entering_cw]() {
                Application::GetInstance().SetWgCwMode(entering_cw); // WG_CW_ISOLATION_TONE_V1
                Board::GetInstance().GetDisplay()->ShowNotification(
                    entering_cw ? "CW MODE" : "XIAOZHI MODE", 3000);
                WgCwLiveUi::SetEnabled(entering_cw); // WG_CW_LIVE_UI_V1
            });
        }

        if (cw_mode_ && cw_decoder_.Tick(now)) EmitCwSnapshot(); // WG_CW_LIVE_UI_V1
        if (!cw_mode_ || !cw_changed) return;
        if (cw_key_.stable_pressed) {
            Application::GetInstance().Schedule([](){ Application::GetInstance().SetWgCwKey(true); }); // WG_CW_ISOLATION_TONE_V1
            cw_pressed_us_ = now;
            cw_decoder_.Down(); // WG_CW_LIVE_UI_V1
            EmitCwSnapshot();
            ESP_LOGI("WG_CW", "KEY DOWN t_us=%lld", static_cast<long long>(now));
        } else {
            Application::GetInstance().Schedule([](){ Application::GetInstance().SetWgCwKey(false); }); // WG_CW_ISOLATION_TONE_V1
            const int64_t held_ms = cw_pressed_us_ ? (now - cw_pressed_us_) / 1000 : 0;
            ESP_LOGI("WG_CW", "KEY UP t_us=%lld duration_ms=%lld",
                static_cast<long long>(now), static_cast<long long>(held_ms));
            cw_pressed_us_ = 0;
            cw_decoder_.Up(static_cast<uint32_t>(held_ms), now); // WG_CW_LIVE_UI_V1
            EmitCwSnapshot();
        }
    }
};
