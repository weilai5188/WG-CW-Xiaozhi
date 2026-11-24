#pragma once
// WG_CW_LIVE_UI_V2: 240x240 CW overlay; LCD/GPIO/audio/XiaoZhi untouched.
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

    // Application scheduled task only. DisplayLockGuard protects all LVGL calls.
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

        // The MORSE strip is strictly ONE line; only the suffix changes when full.
        // Trimming happens in this label only; decoded text has its own viewport.
        const std::string morse_line = Tail(s.morse, kMorseVisibleChars);
        lv_label_set_text(morse_, morse_line.empty() ? "-" : morse_line.c_str());

        // Independent TEXT window; never shares the Morse scroll offset.
        const std::string text_line = Tail(s.decoded, kTextVisibleChars);
        lv_label_set_text(decoded_, text_line.empty() ? "-" : text_line.c_str());
        lv_label_set_text(key_, s.pressed ? "KEY DOWN" : "KEY UP");
        lv_obj_set_style_text_color(key_, lv_color_hex(s.pressed ? 0x08765D : 0x536E85), 0);
        char buf[48];
        std::snprintf(buf, sizeof(buf), "LAST %ums  |  600Hz", s.last_ms);
        lv_label_set_text(info_, buf);
        lv_obj_move_foreground(panel_);
    }

private:
    // 29 ASCII Morse symbols are conservative for the default 14px LCD font
    // inside the 202px text area. We do NOT use auto-scroll or wrapping.
    static constexpr std::size_t kMorseVisibleChars = 29;
    static constexpr std::size_t kTextVisibleChars = 38;

    inline static lv_obj_t* panel_ = nullptr;
    inline static lv_obj_t* header_ = nullptr;
    inline static lv_obj_t* morse_ = nullptr;
    inline static lv_obj_t* decoded_ = nullptr;
    inline static lv_obj_t* key_ = nullptr;
    inline static lv_obj_t* info_ = nullptr;

    static std::string Tail(const std::string& str, std::size_t limit) {
        if (str.size() <= limit) return str;
        return str.substr(str.size() - limit);
    }

    static lv_obj_t* PlainLabel(lv_obj_t* parent, int x, int y,
                                int w, int h, const char* value, uint32_t color,
                                lv_label_long_mode_t mode = LV_LABEL_LONG_CLIP) {
        lv_obj_t* label = lv_label_create(parent);
        lv_obj_set_pos(label, x, y);
        lv_obj_set_size(label, w, h);
        lv_label_set_long_mode(label, mode);
        lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
        lv_label_set_text(label, value);
        return label;
    }

    static lv_obj_t* Box(int x, int y, int w, int h, uint32_t bg,
                         uint32_t border = 0, int border_width = 0, int radius = 8) {
        lv_obj_t* obj = lv_obj_create(panel_);
        lv_obj_set_pos(obj, x, y);
        lv_obj_set_size(obj, w, h);
        lv_obj_set_style_radius(obj, radius, 0);
        lv_obj_set_style_border_width(obj, border_width, 0);
        if (border_width) lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(obj, 0, 0);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        return obj;
    }

    static void Divider(int y) {
        // A 1px simple object avoids dependence on custom theme line widgets.
        Box(12, y, 216, 1, 0xD7E3EF, 0, 0, 0);
    }

    static void Create() {
        panel_ = lv_obj_create(lv_screen_active());
        lv_obj_set_pos(panel_, 0, 0);
        lv_obj_set_size(panel_, 240, 240);
        lv_obj_set_style_radius(panel_, 0, 0);
        lv_obj_set_style_border_width(panel_, 0, 0);
        lv_obj_set_style_pad_all(panel_, 0, 0);
        lv_obj_set_style_bg_color(panel_, lv_color_hex(0xF5F8FC), 0);
        lv_obj_set_style_bg_opa(panel_, LV_OPA_COVER, 0);
        lv_obj_clear_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);

        header_ = PlainLabel(panel_, 13, 10, 154, 20, "CW LIVE", 0x185487);
        Box(188, 9, 39, 20, 0xDCF6E8, 0, 0, 10);
        PlainLabel(panel_, 198, 11, 28, 17, "RX", 0x08765D);
        Divider(35);

        PlainLabel(panel_, 13, 42, 204, 17, "MORSE", 0x67839A);
        lv_obj_t* morse_card = Box(12, 60, 216, 42, 0xE4F0FB);
        // This label uses fixed height + CLIP, and has no wrap/scroll animation.
        morse_ = PlainLabel(morse_card, 8, 11, 200, 20, "-", 0x14558B);
        lv_obj_set_style_text_letter_space(morse_, 0, 0);

        PlainLabel(panel_, 13, 110, 204, 17, "TEXT", 0x67839A);
        lv_obj_t* text_card = Box(12, 128, 216, 51, 0xFFFFFF, 0xD7E3EF, 1);
        // Separate two-line text field. Its updates never move the Morse strip.
        decoded_ = PlainLabel(text_card, 8, 6, 200, 39, "-", 0x18364F, LV_LABEL_LONG_WRAP);
        Divider(188);
        key_ = PlainLabel(panel_, 13, 194, 132, 20, "KEY UP", 0x536E85);
        PlainLabel(panel_, 186, 194, 45, 18, "600Hz", 0x67839A);
        info_ = PlainLabel(panel_, 13, 216, 214, 19, "LAST 0ms  |  600Hz", 0x67839A);
        lv_obj_move_foreground(panel_);
    }
};
