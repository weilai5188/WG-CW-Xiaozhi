#!/usr/bin/env python3
"""Regenerate the LVGL resource header from the 16 editable RGBA frames (requires Pillow)."""
from pathlib import Path
import sys
from PIL import Image

ART = Path(__file__).resolve().parent / "art"
NAMES = (
    "idle", "listening", "thinking", "speaking", "playback", "happy",
    "grateful", "reminder", "connecting", "network", "error", "sleepy",
    "idle_blink", "thinking_pulse", "speaking_pulse", "playback_pulse",
)
WIDTH, HEIGHT = 180, 140
ALIASES = {
    "listening": "listening", "speaking": "speaking",
    "thinking": "thinking", "confused": "thinking",
    "playing": "playback", "playback": "playback", "reading": "playback",
    "happy": "happy", "laughing": "happy", "funny": "happy", "delicious": "happy",
    "loving": "grateful", "kissy": "grateful", "grateful": "grateful", "thanks": "grateful",
    "winking": "reminder", "silly": "reminder", "surprised": "reminder",
    "shocked": "reminder", "embarrassed": "reminder", "reminder": "reminder",
    "connecting": "connecting", "cloud_arrow_down": "connecting", "download": "connecting",
    "network": "network", "connected": "network", "disconnected": "network",
    "error": "error", "angry": "error", "sad": "error", "crying": "error",
    "triangle_exclamation": "error", "circle_xmark": "error", "cloud_slash": "error",
    "sleepy": "sleepy", "relaxed": "idle", "confident": "idle", "cool": "idle",
}


def encode(path):
    rgba = Image.open(path).convert("RGBA")
    if rgba.size != (180, 180):
        raise ValueError(f"{path.name} requires a 180 x 180 canvas")
    rgba = rgba.crop((0, 20, WIDTH, 20 + HEIGHT))
    indexed = rgba.quantize(colors=255, method=Image.Quantize.FASTOCTREE)
    raw_palette = indexed.getpalette("RGBA")
    entries = [(0, 0, 0, 0)] * 256
    for i in range(len(raw_palette) // 4):
        r, g, b, a = raw_palette[4*i:4*i+4]
        entries[i] = (b, g, r, a)
    indices = bytes(i if a else 255 for i, a in zip(indexed.tobytes(), rgba.getchannel("A").tobytes()))
    return bytes(channel for entry in entries for channel in entry) + indices


def generate(destination):
    with destination.open("w", encoding="utf-8", newline="\n") as output:
        output.write("""#pragma once

#include <lvgl.h>
#include <cstring>

namespace WgCwFaces {

inline constexpr int kFaceWidth = 180;
inline constexpr int kFaceHeight = 140;

// Compiled LVGL I8 sprites. A BGRA palette precedes the pixel indices.
""")
        for name in NAMES:
            data = encode(ART / (name + ".png"))
            output.write(f"alignas(4) static const uint8_t {name}_pixels[] = {{\n")
            for i in range(0, len(data), 24):
                line = " ".join(f"0x{b:02x}," for b in data[i:i+24])
                output.write(f"    {line}\n")
            output.write(f"""}};
inline const lv_image_dsc_t {name} = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_I8,
        .flags = 0,
        .w = kFaceWidth,
        .h = kFaceHeight,
        .stride = kFaceWidth,
        .reserved_2 = 0,
    }},
    .data_size = sizeof({name}_pixels),
    .data = {name}_pixels,
    .reserved = nullptr,
    .reserved_2 = nullptr,
}};

""")
        output.write("""inline const lv_image_dsc_t* Find(const char* emotion) {
    static const struct {
        const char* name;
        const lv_image_dsc_t* image;
    } mapping[] = {
""")
        for key, value in ALIASES.items():
            output.write(f'        {{"{key}", &{value}}},\n')
        output.write("""    };
    for (const auto& entry : mapping) {
        if (std::strcmp(emotion, entry.name) == 0) {
            return entry.image;
        }
    }
    return nullptr;
}

struct AnimationStep {
    const lv_image_dsc_t* image;
    uint16_t duration_ms;
    int8_t y;
};

struct Animation {
    const AnimationStep* steps;
    uint8_t count;
};

inline Animation GetAnimation(const lv_image_dsc_t* face) {
    static constexpr AnimationStep blink[] = {{&idle, 2200, 0}, {&idle_blink, 170, 0}};
    static constexpr AnimationStep thoughts[] = {{&thinking, 290, 0}, {&thinking_pulse, 320, 0}};
    static constexpr AnimationStep speech[] = {{&speaking, 210, 0}, {&speaking_pulse, 220, -2}};
    static constexpr AnimationStep media[] = {{&playback, 230, 0}, {&playback_pulse, 240, -2}};
    // A null frame reuses the base sprite and changes only its position.
    static constexpr AnimationStep bounce[] = {{nullptr, 440, 0}, {nullptr, 230, -2},
                                                {nullptr, 210, -4}, {nullptr, 230, -2}};
    static constexpr AnimationStep breathe[] = {{nullptr, 740, 0}, {nullptr, 420, -1},
                                                 {nullptr, 520, -2}, {nullptr, 420, -1}};
    if (face == &idle) return {blink, 2};
    if (face == &thinking) return {thoughts, 2};
    if (face == &speaking) return {speech, 2};
    if (face == &playback) return {media, 2};
    if (face == &sleepy || face == &network || face == &error) return {breathe, 4};
    return {bounce, 4};
}

}  // namespace WgCwFaces
""")


if __name__ == "__main__":
    generate(Path(sys.argv[1]) if len(sys.argv) > 1 else Path("wg_cw_faces.h"))
