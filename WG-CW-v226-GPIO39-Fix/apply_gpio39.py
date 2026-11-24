#!/usr/bin/env python3
"""Safe change: GPIO39 long press 2 s toggles CW; GPIO17 straight key retained.
No I2S/audio changes. Run inside WG-CW-Xiaozhi-226 repo.
"""
import argparse
from pathlib import Path
import sys

TARGET=Path('main/boards/bread-compact-wifi-lcd/cw_mode_controller.h')
BACKUP=TARGET.with_suffix(TARGET.suffix+'.pre-gpio39.bak')

def transform(text):
    if 'GPIO39 hold 2 seconds' in text:
        raise ValueError('Already applied: GPIO39 single key detected')
    repl=[
      ('GPIO38+GPIO39 hold for 2 seconds: toggle AI/CW monitoring mode.', 'GPIO39 hold 2 seconds: toggle AI/CW monitoring mode.'),
      ('(1ULL << 17) | (1ULL << 38) | (1ULL << 39)', '(1ULL << 17) | (1ULL << 39)'),
      ('        InitKey(volume_up_, GPIO_NUM_38, now);\n', ''),
      ('GPIO38+39 hold 2s to toggle mode', 'GPIO39 hold 2s to toggle mode'),
      ('    Key volume_up_;\n', ''),
      ('        UpdateKey(volume_up_, now);\n', ''),
      ('        const bool both = volume_up_.stable_pressed && volume_down_.stable_pressed;', '        const bool button_held = volume_down_.stable_pressed;'),
      ('        if (!both) {', '        if (!button_held) {'),
    ]
    for a,b in repl:
        count=text.count(a)
        if count!=1:
            raise ValueError('Expected exactly one anchor, got %d: %r' % (count,a))
        text=text.replace(a,b)
    if 'GPIO_NUM_38' in text or 'volume_up_' in text or 'if (!both)' in text:
        raise ValueError('Unexpected old GPIO38 usage remains')
    return text

p=argparse.ArgumentParser()
p.add_argument('--check',action='store_true')
p.add_argument('--rollback',action='store_true')
a=p.parse_args()
if not TARGET.is_file():
    sys.exit('ERROR: Run this from D:/Android/WG-CW-Xiaozhi-226')
if a.rollback:
    if not BACKUP.exists():sys.exit('ERROR: Backup does not exist')
    TARGET.write_bytes(BACKUP.read_bytes())
    print('Rollback OK. Old file restored.')
else:
    source=TARGET.read_text(encoding='utf-8')
    try: revised=transform(source)
    except ValueError as e: sys.exit('ERROR: '+str(e))
    if a.check:
        print('CHECK OK: Existing 38+39 controller matches; can switch to GPIO39 single key.')
    else:
        if BACKUP.exists():sys.exit('ERROR: Backup already exists; stop to avoid overwriting it')
        BACKUP.write_bytes(TARGET.read_bytes())
        TARGET.write_text(revised,encoding='utf-8',newline='\n')
        print('APPLIED: GPIO39 long press 2 s; GPIO17 unchanged; GPIO38 untouched.')
        print('No audio output was added by this patch.')
