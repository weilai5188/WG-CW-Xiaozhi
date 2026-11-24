#pragma once
// WG_CW_LIVE_UI_V1. LVGL overlay; no changes to XiaoZhi LCD classes.
#include <lvgl.h>
#include <string>
#include <cstdio>
#include "display.h"
#include "board.h"

class WgCwLiveUi {
public:
    struct Snapshot {
        bool pressed = false;
        unsigned last_ms = 0;
        std::string morse;
        std::string decoded;
        std::string current;
    };

    // Call only from the scheduled application task; locks LVGL via DisplayLockGuard.
    static void SetEnabled(bool enabled) {
        auto* display = Board::GetInstance().GetDisplay();
        if (!display) return;
        DisplayLockGuard guard(display);
        if (enabled) {
            if (!panel_) Create();
            if (panel_) lv_obj_move_foreground(panel_);
        } else if (panel_) {
            lv_obj_delete(panel_);
            panel_ = nullptr;
            header_ = morse_ = decoded_ = key_ = info_ = nullptr;
        }
    }

    static void Refresh(const Snapshot& s) {
        auto* display = Board::GetInstance().GetDisplay();
        if (!display) return;
        DisplayLockGuard guard(display);
        if (!panel_) return;
        lv_label_set_text(header_, "CW  LIVE DECODE");
        lv_label_set_text(morse_, s.morse.empty() ? "-" : s.morse.c_str());
        lv_label_set_text(decoded_, s.decoded.empty() ? "-" : s.decoded.c_str());
        lv_label_set_text(key_, s.pressed ? "KEY  DOWN" : "KEY  UP");
        lv_obj_set_style_text_color(key_, lv_color_hex(s.pressed ? 0x008D69 : 0x48627A), 0);
        char buf[64];
        snprintf(buf, sizeof(buf), "LAST %ums   |   600Hz", s.last_ms);
        lv_label_set_text(info_, buf);
        lv_obj_move_foreground(panel_);
    }

private:
    inline static lv_obj_t* panel_ = nullptr;
    inline static lv_obj_t* header_ = nullptr;
    inline static lv_obj_t* morse_ = nullptr;
    inline static lv_obj_t* decoded_ = nullptr;
    inline static lv_obj_t* key_ = nullptr;
    inline static lv_obj_t* info_ = nullptr;

    static lv_obj_t* Label(int y, const char* value, uint32_t color) {
        auto* label = lv_label_create(panel_);
        lv_obj_set_width(label, 216);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_pos(label, 12, y);
        lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
        lv_label_set_text(label, value);
        return label;
    }

    static void Create() {
        panel_ = lv_obj_create(lv_screen_active());
        lv_obj_set_size(panel_, 240, 240);
        lv_obj_set_pos(panel_, 0, 0);
        lv_obj_set_style_radius(panel_, 0, 0);
        lv_obj_set_style_border_width(panel_, 0, 0);
        lv_obj_set_style_pad_all(panel_, 0, 0);
        lv_obj_set_style_bg_color(panel_, lv_color_hex(0xF5F8FC), 0);
        lv_obj_set_style_bg_opa(panel_, LV_OPA_COVER, 0);
        lv_obj_clear_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
        header_ = Label(12, "CW  LIVE DECODE", 0x125795);
        Label(51, "MORSE", 0x67819A);
        morse_ = Label(73, "-", 0x123B61);
        Label(109, "TEXT", 0x67819A);
        decoded_ = Label(133, "-", 0x152E46);
        key_ = Label(174, "KEY  UP", 0x48627A);
        info_ = Label(203, "LAST 0ms   |   600Hz", 0x67819A);
        lv_obj_move_foreground(panel_);
    }
};
