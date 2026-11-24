#!/usr/bin/env python3
"""Apply conservative WG-CW GPIO17 + 38/39 mode toggle patch to XiaoZhi v2.2.6.

Run from repo root: python apply_wg_cw_v226.py
Changes ONLY the bread-compact-wifi-lcd board translation unit and adds one header.
"""
from pathlib import Path
import shutil
import sys

root = Path.cwd()
board = root / "main/boards/bread-compact-wifi-lcd"
target = board / "compact_wifi_board_lcd.cc"
header_src = Path(__file__).resolve().parent / "cw_mode_controller.h"
header_dst = board / "cw_mode_controller.h"
if not target.is_file():
    sys.exit(f"ERROR: not in XiaoZhi v2.2.6 repo root: {target}")
if not header_src.is_file():
    sys.exit("ERROR: cw_mode_controller.h must be next to the patch script")
original = target.read_text(encoding="utf-8")
if 'WgCwModeController' in original:
    sys.exit("Already patched; no changes made")

patterns = [
    ('#include "config.h"', '#include "config.h"\n#include "cw_mode_controller.h"'),
    ('    Button boot_button_;\n', '    Button boot_button_;\n    WgCwModeController cw_mode_;\n'),
    ('        boot_button_.OnClick([this]() {\n', '        boot_button_.OnClick([this]() {\n            if (cw_mode_.IsCwMode()) return;\n'),
    ('        InitializeTools();\n', '        InitializeTools();\n        cw_mode_.Start();\n'),
]
updated = original
for old, new in patterns:
    occurrences = updated.count(old)
    if occurrences != 1:
        sys.exit(f"ERROR: expected 1 occurrence, got {occurrences} for {old!r}; nothing written")
    updated = updated.replace(old, new, 1)
backup = target.with_suffix(target.suffix + ".wg-pre-cw.bak")
if not backup.exists():
    shutil.copy2(target, backup)
header_dst.write_bytes(header_src.read_bytes())
target.write_text(updated, encoding="utf-8")
print("SUCCESS: board modified, CW controller added")
print(f"Backup: {backup}")
print("Mode: volume GPIO38+39 both LOW for 2s, GPIO17 straight key LOW")
print("Note: audio remains owned by XiaoZhi in this first hardware test")
