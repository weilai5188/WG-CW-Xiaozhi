#!/usr/bin/env python3
"""Safe additive patch to the *tested* CW controller only.
Does not touch ESP-IDF app/audio/network/display code. Supports --check/--rollback.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

MARK = 'WG_CW_USB_LINK_V1'
ROOT = Path(__file__).resolve().parents[1]
FOLDER = Path('main/boards/bread-compact-wifi-lcd')
CONTROLLER = ROOT / FOLDER / 'cw_mode_controller.h'
BRIDGE = ROOT / FOLDER / 'wg_cw_usb_bridge.h'
BACKUP = ROOT / '.wg_cw_usb_link_backup'
ASSET = Path(__file__).resolve().parent / 'wg_cw_usb_bridge.h'

def sha(b): return hashlib.sha256(b).hexdigest()
def replace_once(s,pattern,sub):
    result,n = re.subn(pattern, sub, s, count=1)
    if n!=1: raise ValueError(f'Expected one match: {pattern!r}; got {n}')
    return result

def patch(s):
    if MARK in s: raise ValueError('Already patched; use --rollback if needed')
    for token in ('SetWgCwMode(', 'SetWgCwKey(true)', 'SetWgCwKey(false)',
                  'cw_decoder_.Down()', 'cw_decoder_.Up(', 'GPIO_NUM_39', 'GPIO_NUM_17'):
        if token not in s: raise ValueError('Missing verified firmware feature: '+token)
    if 'GPIO_NUM_38' in s or 'GPIO_NUM_40' in s:
        raise ValueError('Unexpected GPIO38/40 reference in controller; refuse patch')
    s=replace_once(s, r'(#include "wg_cw_decoder.h"[^\n]*\n)',
         lambda m:m.group(1)+'#include "wg_cw_usb_bridge.h" // '+MARK+'\n')
    s=replace_once(s, r'(esp_timer_start_periodic\(timer_,\s*10000\)\);)',
         lambda m:m.group(1)+'\n        WgCwUsbBridge::Get().Start(); // '+MARK)
    s=replace_once(s, r'(cw_mode_\s*=\s*!cw_mode_;)',
         lambda m:m.group(1)+'\n            WgCwUsbBridge::Get().Mode(cw_mode_); // '+MARK)
    s=replace_once(s, r'(cw_decoder_\.Down\(\);[^\n]*)',
         lambda m:m.group(1)+'\n            WgCwUsbBridge::Get().Down(now); // '+MARK)
    s=replace_once(s, r'(cw_decoder_\.Up\(static_cast<uint32_t>\(held_ms\),\s*now\);[^\n]*)',
         lambda m:m.group(1)+'\n            WgCwUsbBridge::Get().Up(now, static_cast<unsigned>(held_ms)); // '+MARK)
    return s

def main():
    ap=argparse.ArgumentParser()
    grp=ap.add_mutually_exclusive_group()
    grp.add_argument('--check',action='store_true')
    grp.add_argument('--rollback',action='store_true')
    args=ap.parse_args()
    if args.rollback:
        info=json.loads((BACKUP/'manifest.json').read_text('utf-8'))
        if sha(CONTROLLER.read_bytes())!=info['patched']: raise ValueError('Controller modified after patch; rollback refused')
        if sha(BRIDGE.read_bytes())!=info['asset']: raise ValueError('Bridge modified after patch; rollback refused')
        original=(BACKUP/'cw_mode_controller.h').read_bytes()
        if sha(original)!=info['original']: raise ValueError('Backup checksum mismatch')
        CONTROLLER.write_bytes(original)
        BRIDGE.unlink()
        import shutil
        shutil.rmtree(BACKUP)
        print('ROLLED BACK: CW USB changes undone')
        return
    if BACKUP.exists(): raise ValueError('Existing USB backup found; do not overwrite')
    if BRIDGE.exists(): raise ValueError('USB header already exists; do not overwrite')
    original=CONTROLLER.read_bytes()
    newline='\r\n' if b'\r\n' in original else '\n'
    final_newline=original.endswith(b'\n')
    text=original.decode('utf-8').replace('\r\n','\n')
    modified=patch(text)
    if not final_newline: modified=modified.rstrip('\n')
    patched=modified.replace('\n',newline).encode('utf-8')
    asset=ASSET.read_bytes()
    if args.check:
        print('CHECK OK: CW isolation + GPIO39 + live UI markers found. No modification.')
        return
    BACKUP.mkdir()
    (BACKUP/'cw_mode_controller.h').write_bytes(original)
    (BACKUP/'manifest.json').write_text(json.dumps({'original':sha(original),'patched':sha(patched),'asset':sha(asset)},indent=2),'utf-8')
    try:
        BRIDGE.write_bytes(asset)
        CONTROLLER.write_bytes(patched)
    except Exception:
        CONTROLLER.write_bytes(original)
        if BRIDGE.exists(): BRIDGE.unlink()
        import shutil
        shutil.rmtree(BACKUP)
        raise
    print('APPLIED: CW controller + 1 USB bridge header; original backed up')
    print('UNCHANGED: audio, display, Wi-Fi, GPIO17/39/40 pin assignments')

if __name__=='__main__':
    try: main()
    except Exception as e:
        print('ERROR:',e)
        raise SystemExit(1)
