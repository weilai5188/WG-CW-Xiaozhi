#pragma once

// WG-CW first hardware test for XiaoZhi v2.2.6 / bread-compact-wifi-lcd.
// GPIO39 hold 2 seconds: toggle AI/CW monitoring mode.
// GPIO17 to GND: CW straight key, active LOW, 3 ms CW debounce.
// CW side tone is controlled directly; UI stays on the application task.

#include <cstdint>
#include <atomic>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include "application.h"
#include "board.h"
#include "wg_cw_decoder.h" // WG_CW_LIVE_UI_V1
#include "wg_cw_usb_bridge.h" // WG_CW_USB_LINK_V1

class WgCwModeController {
public:
    bool IsCwMode() const { return cw_mode_; }

    void RequestEnterCw() { enter_requested_.store(true); }

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
        ESP_ERROR_CHECK(esp_timer_start_periodic(timer_, 1000));
        WgCwUsbBridge::Get().Start(); // WG_CW_USB_LINK_V1
        ESP_LOGI("WG_CW", "Ready: GPIO17 key, GPIO39 hold 2s to toggle mode");
    }

private:
    struct Key {
        gpio_num_t pin = GPIO_NUM_NC;
        bool raw_pressed = false;
        bool stable_pressed = false;
        int64_t raw_changed_us = 0;
    };

    static constexpr int64_t kCwDebounceUs = 3000;
    static constexpr int64_t kButtonDebounceUs = 30000;
    static constexpr int64_t kComboHoldUs = 2000000;

    esp_timer_handle_t timer_ = nullptr;
    Key cw_key_;
    Key volume_down_;
    std::atomic<bool> cw_mode_{false};
    std::atomic<bool> cw_ready_{false};
    std::atomic<bool> enter_requested_{false};
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

    static bool UpdateKey(Key& key, int64_t now, int64_t debounce_us) {
        bool raw = gpio_get_level(key.pin) == 0;
        if (raw != key.raw_pressed) {
            key.raw_pressed = raw;
            key.raw_changed_us = now;
        }
        if (raw != key.stable_pressed && now - key.raw_changed_us >= debounce_us) {
            key.stable_pressed = raw;
            return true;
        }
        return false;
    }

    void ChangeMode(bool entering_cw) {
        cw_ready_.store(false);
        cw_mode_.store(entering_cw);
        Application::GetInstance().SetWgCwKey(false);
        WgCwUsbBridge::Get().Mode(cw_mode_); // WG_CW_USB_LINK_V1
        cw_pressed_us_ = 0;
        cw_decoder_.Reset(); // WG_CW_LIVE_UI_V1
        ESP_LOGI("WG_CW", "MODE=%s", entering_cw ? "CW" : "XIAOZHI");
        // Mode transition and UI stay on XiaoZhi main task.
        Application::GetInstance().Schedule([this, entering_cw]() {
            Application::GetInstance().SetWgCwMode(entering_cw); // WG_CW_ISOLATION_TONE_V1
            Board::GetInstance().GetDisplay()->ShowNotification(
                entering_cw ? "CW MODE" : "XIAOZHI MODE", 3000);
            WgCwLiveUi::SetEnabled(entering_cw); // WG_CW_LIVE_UI_V1
            cw_ready_.store(entering_cw && cw_mode_.load());
        });
    }

    void Poll() {
        const int64_t now = esp_timer_get_time();
        const bool cw_changed = UpdateKey(cw_key_, now, kCwDebounceUs);
        UpdateKey(volume_down_, now, kButtonDebounceUs);

        const bool button_held = volume_down_.stable_pressed;
        if (!button_held) {
            combo_started_us_ = 0;
            combo_latched_ = false;
        } else if (combo_started_us_ == 0) {
            combo_started_us_ = now;
        } else if (!combo_latched_ && now - combo_started_us_ >= kComboHoldUs) {
            combo_latched_ = true;
            ChangeMode(!cw_mode_.load());
        }

        if (enter_requested_.exchange(false) && !cw_mode_.load()) {
            ChangeMode(true);
        }

        if (cw_mode_ && cw_decoder_.Tick(now)) EmitCwSnapshot(); // WG_CW_LIVE_UI_V1
        if (!cw_mode_ || !cw_ready_.load() || !cw_changed) return;
        if (cw_key_.stable_pressed) {
            Application::GetInstance().SetWgCwKey(true); // WG_CW_ISOLATION_TONE_V1
            cw_pressed_us_ = cw_key_.raw_changed_us;
            cw_decoder_.Down(); // WG_CW_LIVE_UI_V1
            WgCwUsbBridge::Get().Down(cw_pressed_us_); // WG_CW_USB_LINK_V1
            EmitCwSnapshot();
        } else {
            Application::GetInstance().SetWgCwKey(false); // WG_CW_ISOLATION_TONE_V1
            if (!cw_pressed_us_) return;
            const int64_t edge_us = cw_key_.raw_changed_us;
            const int64_t held_ms = (edge_us - cw_pressed_us_) / 1000;
            cw_pressed_us_ = 0;
            cw_decoder_.Up(static_cast<uint32_t>(held_ms), edge_us); // WG_CW_LIVE_UI_V1
            WgCwUsbBridge::Get().Up(edge_us, static_cast<unsigned>(held_ms)); // WG_CW_USB_LINK_V1
            EmitCwSnapshot();
        }
    }
};
