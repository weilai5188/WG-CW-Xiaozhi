#!/usr/bin/env python3
"""WG CW live decode overlay installer. Only changes the existing controller header.
Does not touch the audio service, application, LCD drivers, or hardware pins.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

MARK='WG_CW_LIVE_UI_V1'
TARGET=Path('main/boards/bread-compact-wifi-lcd/cw_mode_controller.h')
DEST=Path('main/boards/bread-compact-wifi-lcd')
HEADERS=['wg_cw_live_ui.h','wg_cw_decoder.h']
BACKUP=Path('.wg_cw_live_ui_backup')

def sha(data): return hashlib.sha256(data).hexdigest()
def sub_once(s,old,new):
    n=s.count(old)
    if n!=1: raise ValueError(f'Expected exactly 1 anchor {old[:85]!r}, found {n}')
    return s.replace(old,new,1)

def patch(s):
    if MARK in s: raise ValueError('Already applied; use --rollback first')
    if ('GPIO39 hold 2 seconds' not in s or
        'SetWgCwMode(' not in s or 'SetWgCwKey(true)' not in s or
        'SetWgCwKey(false)' not in s or
        'GPIO_NUM_38' in s):
        raise ValueError('Expected tested GPIO39 + CW isolation/tone source not found; no changes')
    s=sub_once(s,'#include "board.h"',
        '#include "board.h"\n#include "wg_cw_decoder.h" // '+MARK)
    s=sub_once(s,'    int64_t cw_pressed_us_ = 0;', '''    int64_t cw_pressed_us_ = 0;
    WgCwDecoder cw_decoder_; // WG_CW_LIVE_UI_V1

    void EmitCwSnapshot() {
        const auto snapshot = cw_decoder_.Snapshot();
        Application::GetInstance().Schedule([snapshot]() {
            WgCwLiveUi::Refresh(snapshot);
        });
    }
''')
    s=sub_once(s,'            cw_mode_ = !cw_mode_;\n            cw_pressed_us_ = 0;',
       '            cw_mode_ = !cw_mode_;\n            cw_pressed_us_ = 0;\n            cw_decoder_.Reset(); // '+MARK)
    # UI appears / disappears on the main scheduled task, after CW state switch.
    pat=r'(Board::GetInstance\(\)\.GetDisplay\(\)->ShowNotification\(\s*entering_cw\s*\?\s*"CW MODE"\s*:\s*"XIAOZHI MODE"\s*,\s*3000\s*\);)'
    s,n=re.subn(pat, r'\1\n                WgCwLiveUi::SetEnabled(entering_cw); // '+MARK,s,count=1)
    if n!=1: raise ValueError('Cannot find ShowNotification block in mode callback')
    s=sub_once(s,'        if (!cw_mode_ || !cw_changed) return;',
        '        if (cw_mode_ && cw_decoder_.Tick(now)) EmitCwSnapshot(); // '+MARK+'\n        if (!cw_mode_ || !cw_changed) return;')
    s=sub_once(s,'            cw_pressed_us_ = now;',
        '            cw_pressed_us_ = now;\n            cw_decoder_.Down(); // '+MARK+'\n            EmitCwSnapshot();')
    # insert at end of release code *after* existing cw_pressed_us_=0 (not the mode-toggle one)
    marker='            cw_pressed_us_ = 0;'
    # Last occurrence in controller must be key-up branch.
    if s.count(marker)!=2: raise ValueError('Controller CW press-state structure differs from expected; cannot safely patch release')
    i=s.rfind(marker)
    s=s[:i]+s[i:].replace(marker,marker+'''\n            cw_decoder_.Up(static_cast<uint32_t>(held_ms), now); // WG_CW_LIVE_UI_V1
            EmitCwSnapshot();''',1)
    return s

def root_from(script): return script.resolve().parent.parent

def run(args):
    script_dir=Path(__file__).resolve().parent
    root=root_from(Path(__file__))
    target=root/TARGET
    if not target.is_file(): raise ValueError('Controller missing: '+str(target))
    if args.rollback:
        manifest=BACKUP/'manifest.json'
        if not manifest.is_file(): raise ValueError('No saved live UI backup in repository')
        info=json.loads(manifest.read_text('utf-8'))
        cur=target.read_bytes()
        if sha(cur)!=info['patched_sha256']:
            raise ValueError('Controller has changed since installation; rollback refused to avoid losing edits')
        for filename in HEADERS:
            dst=root/DEST/filename
            if not dst.is_file() or sha(dst.read_bytes())!=info['assets'][filename]:
                raise ValueError(f'{filename} changed/missing; rollback refused')
        original=(BACKUP/'cw_mode_controller.h').read_bytes()
        if sha(original)!=info['original_sha256']:
            raise ValueError('Backup checksum mismatch')
        target.write_bytes(original)
        for filename in HEADERS: (root/DEST/filename).unlink()
        import shutil
        shutil.rmtree(BACKUP)
        print('ROLLED BACK: original controller restored; 2 added headers removed')
        return
    if BACKUP.exists(): raise ValueError('Existing backup found; do not overwrite it')
    for filename in HEADERS:
        if (root/DEST/filename).exists(): raise ValueError(f'Target {filename} already exists; no changes')
    original=target.read_bytes()
    text=original.decode('utf-8')
    newline='\r\n' if b'\r\n' in original else '\n'
    ending=original.endswith((b'\n',b'\r'))
    out=patch(text.replace('\r\n','\n'))
    if not ending: out=out.rstrip('\n')
    patched=out.replace('\n',newline).encode('utf-8')
    assets={p:(script_dir/p).read_bytes() for p in HEADERS}
    if args.check:
        print('CHECK OK: GPIO39 + isolation/tone source matched; controller-only overlay patch ready')
        return
    BACKUP.mkdir()
    (BACKUP/'cw_mode_controller.h').write_bytes(original)
    manifest={'original_sha256':sha(original),'patched_sha256':sha(patched),
              'assets':{p:sha(content) for p,content in assets.items()}}
    (BACKUP/'manifest.json').write_text(json.dumps(manifest,indent=2),'utf-8')
    try:
        for filename,content in assets.items(): (root/DEST/filename).write_bytes(content)
        target.write_bytes(patched)
    except Exception:
        target.write_bytes(original)
        for filename in HEADERS:
            dst=root/DEST/filename
            if dst.exists(): dst.unlink()
        import shutil
        shutil.rmtree(BACKUP)
        raise
    print('APPLIED: 1 controller modified + 2 new CW headers; backup:', str(BACKUP))
    print('NO changes to LCD driver, GPIO pins, sound, application, or WiFi')

if __name__=='__main__':
    ap=argparse.ArgumentParser()
    grp=ap.add_mutually_exclusive_group()
    grp.add_argument('--check',action='store_true')
    grp.add_argument('--rollback',action='store_true')
    args=ap.parse_args()
    try: run(args)
    except (ValueError, OSError, UnicodeError) as exc:
        print('ERROR:',exc)
        raise SystemExit(1)
