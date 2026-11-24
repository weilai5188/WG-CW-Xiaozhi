#!/usr/bin/env python3
"""WG CW v2.2.6 isolation and sidetone patch. Strict anchor checking and rollback.

Applies only to main/application.{h,cc}, main/audio/audio_service.{h,cc},
main/boards/bread-compact-wifi-lcd/cw_mode_controller.h.
"""
import argparse, hashlib, json, re, sys
from pathlib import Path

TARGETS = [
 'main/application.h','main/application.cc',
 'main/audio/audio_service.h','main/audio/audio_service.cc',
 'main/boards/bread-compact-wifi-lcd/cw_mode_controller.h',
]
BACKUP = Path('.wg_cw_isolation_tone_backup')
MARKER='WG_CW_ISOLATION_TONE_V1'

def fail(message): raise RuntimeError(message)
def replace_once(s, old, new, file):
    n=s.count(old)
    if n!=1: fail(f'{file}: expected 1 occurrence of {old[:100]!r}, found {n}; no files modified')
    return s.replace(old,new,1)
def regex_once(s, pat, repl, file, flags=0):
    new,count=re.subn(pat,repl,s,count=1,flags=flags)
    if count!=1: fail(f'{file}: expected regex {pat[:95]!r}, got {count}; no files modified')
    return new

def patch_app_h(s, f):
    s=replace_once(s,'#include <mutex>', '#include <mutex>\n#include <atomic> // '+MARKER,f)
    s=replace_once(s,'    void StopListening();', '''    void StopListening();

    // WG CW: call on the XiaoZhi main task only; no changes to hardware pins.
    void SetWgCwMode(bool enabled); // WG_CW_ISOLATION_TONE_V1
    void SetWgCwKey(bool pressed);
    bool IsWgCwMode() const { return wg_cw_mode_.load(); }''',f)
    s=replace_once(s,'    AudioService audio_service_;', '    std::atomic<bool> wg_cw_mode_{false}; // '+MARKER+'\n    AudioService audio_service_;',f)
    return s

def patch_app_cc(s,f):
    # Called from scheduled main task, not from 10ms timer callback.
    marker='void Application::StopListening() {'
    code='''// WG_CW_ISOLATION_TONE_V1 - mutually exclusive XiaoZhi / CW operation.
void Application::SetWgCwMode(bool enabled) {
    if (wg_cw_mode_.load() == enabled) return;
    if (enabled) {
        // Gate queued network/state callbacks before starting teardown.
        wg_cw_mode_.store(true);
        audio_service_.SetWgCwTone(false);
        audio_service_.EnableVoiceProcessing(false);
        audio_service_.EnableWakeWordDetection(false);
        audio_service_.ResetDecoder();
        while (audio_service_.PopPacketFromSendQueue()) {}
        if (protocol_) {
            auto state = GetDeviceState();
            if (state == kDeviceStateListening) protocol_->SendStopListening();
            if (state == kDeviceStateSpeaking) AbortSpeaking(kAbortReasonNone);
            if (protocol_->IsAudioChannelOpened()) protocol_->CloseAudioChannel();
        }
        SetDeviceState(kDeviceStateIdle);
        // Idle handler can re-arm wake word; explicitly turn it off last.
        audio_service_.EnableVoiceProcessing(false);
        audio_service_.EnableWakeWordDetection(false);
        ESP_LOGI("WG_CW", "ISOLATION=ON wake=OFF voice=OFF tone=READY");
    } else {
        audio_service_.SetWgCwTone(false);
        wg_cw_mode_.store(false);
        SetDeviceState(kDeviceStateIdle);
        audio_service_.EnableVoiceProcessing(false);
        audio_service_.EnableWakeWordDetection(true);
        ESP_LOGI("WG_CW", "ISOLATION=OFF wake=ON XiaoZhi restored");
    }
}

void Application::SetWgCwKey(bool pressed) {
    if (!wg_cw_mode_.load()) pressed = false;
    audio_service_.SetWgCwTone(pressed);
}

'''
    s=replace_once(s,marker,code+marker,f)
    # Gate async calls that would restart listening and waking.
    for name in ('HandleToggleChatEvent', 'HandleStartListeningEvent', 'HandleWakeWordDetectedEvent', 'ContinueOpenAudioChannel', 'ContinueWakeWordInvoke'):
        pat=r'(void Application::'+name+r'\([^)]*\)\s*\{)'
        s=regex_once(s,pat,r'\1\n    if (wg_cw_mode_.load()) return; // WG_CW_ISOLATION_TONE_V1',f)
    # Protect state transition to Listening, Connecting or Speaking from async MQTT TTS.
    pat=r'(bool Application::SetDeviceState\(DeviceState state\)\s*\{)'
    s=regex_once(s,pat,r'\1\n    if (wg_cw_mode_.load() && (state == kDeviceStateListening || state == kDeviceStateSpeaking || state == kDeviceStateConnecting)) return false; // WG_CW_ISOLATION_TONE_V1',f)
    # The existing idle state turns wake word on: hold it disabled after every state change.
    pat=r'(void Application::HandleStateChangedEvent\(\)\s*\{)'
    s=regex_once(s,pat,r'\1\n    // WG_CW_ISOLATION_TONE_V1: guard standby rearming below.',f)
    # Gate wake word enabling in idle ONLY; other state changes remain unchanged.
    pat=r'(case kDeviceStateIdle\s*:\s*[^;]*?audio_service_\.EnableVoiceProcessing\(false\);\s*)audio_service_\.EnableWakeWordDetection\(true\);'
    # GitHub v2.2.6 uses grouped labels unknown + idle; match from idle to next case without giant regex.
    app_state_start=s.index('void Application::HandleStateChangedEvent()')
    app_state_end=s.index('void Application::Schedule(',app_state_start)
    state_body=s[app_state_start:app_state_end]
    state_body=replace_once(state_body,'audio_service_.EnableWakeWordDetection(true);',
      'audio_service_.EnableWakeWordDetection(!wg_cw_mode_.load()); // WG_CW_ISOLATION_TONE_V1',f)
    s=s[:app_state_start]+state_body+s[app_state_end:]
    # Suppress stray queued SEND_AUDIO events while in CW mode.
    s=replace_once(s,'if (protocol_ && !protocol_->SendAudio(std::move(packet)))',
      'if (wg_cw_mode_.load()) continue; // WG_CW_ISOLATION_TONE_V1\n                if (protocol_ && !protocol_->SendAudio(std::move(packet)))',f)
    return s

def patch_audio_h(s,f):
    s=replace_once(s,'#include <mutex>','#include <mutex>\n#include <atomic> // '+MARKER,f)
    s=replace_once(s,'    void PlaySound(const std::string_view& sound);',
      '    void PlaySound(const std::string_view& sound);\n    void SetWgCwTone(bool enabled); // WG_CW_ISOLATION_TONE_V1',f)
    s=replace_once(s,'    AudioCodec* codec_ = nullptr;',
      '    std::atomic<bool> wg_cw_tone_{false}; // WG_CW_ISOLATION_TONE_V1\n    AudioCodec* codec_ = nullptr;',f)
    return s

def patch_audio_cc(s,f):
    # Generate a continuous CW tone in the EXISTING audio output thread.
    # No GPIO/I2S peripheral reconfiguration; codec->OutputData remains single-writer.
    s=replace_once(s,'void AudioService::AudioOutputTask() {', '''void AudioService::SetWgCwTone(bool enabled) {
    // WG_CW_ISOLATION_TONE_V1. Atomic state is observed by the output task.
    wg_cw_tone_.store(enabled);
    audio_queue_cv_.notify_all();
}

void AudioService::AudioOutputTask() {''',f)
    s=replace_once(s,'audio_queue_cv_.wait(lock, [this]() { return !audio_playback_queue_.empty() || service_stopped_; });', '''// WG_CW_ISOLATION_TONE_V1: CW audio takes exclusive output thread ownership while pressed.
        audio_queue_cv_.wait(lock, [this]() { return !audio_playback_queue_.empty() || service_stopped_ || wg_cw_tone_.load(); });
        if (wg_cw_tone_.load()) {
            // Keep normal AI decoder data from playing in CW mode.
            audio_playback_queue_.clear();
            lock.unlock();
            if (!codec_->output_enabled()) {
                esp_timer_stop(audio_power_timer_);
                esp_timer_start_periodic(audio_power_timer_, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
                codec_->EnableOutput(true);
            }
            // Smooth single-sine, per-frame phase continuity; 600 Hz at codec output rate.
            const int sample_rate = codec_->output_sample_rate();
            if (sample_rate > 0) {
                const int n = sample_rate / 100; // 10ms PCM
                std::vector<int16_t> samples(n);
                constexpr float TWO_PI = 6.2831853071795864769f;
                static float phase = 0.0f;
                const float step = TWO_PI * 600.0f / sample_rate;
                for (int i = 0; i < n; ++i) {
                    samples[i] = static_cast<int16_t>(sinf(phase) * 5000.0f);
                    phase += step;
                    if (phase >= TWO_PI) phase -= TWO_PI;
                }
                // OutputData is blocking; release latency is up to one PCM block.
                codec_->OutputData(samples);
                last_output_time_ = std::chrono::steady_clock::now();
            }
            continue;
        }''',f)
    # At top of .cc prefer include cmath; must be guaranteed unique point.
    s=replace_once(s,'#include "audio_service.h"','#include "audio_service.h"\n#include <cmath> // '+MARKER,f)
    return s

def patch_controller(s,f):
    if 'GPIO39 hold 2 seconds' not in s or 'GPIO_NUM_38' in s:
        fail(f'{f}: expected ef1c0ca GPIO39-only mode controller; not found')
    # ef1c0ca retains the original braced `if (entering_cw) { StopListening(); }`.
    # Replace the WHOLE conditional with a single call taking the real mode.
    # This is essential: an entering-only call never restores XiaoZhi on exit.
    braced = (r'if\s*\(entering_cw\)\s*\{\s*'
              r'Application::GetInstance\(\)\.StopListening\(\);\s*\}')
    single = (r'if\s*\(entering_cw\)\s*'
              r'Application::GetInstance\(\)\.StopListening\(\);')
    if re.search(braced, s):
        s = regex_once(s, braced,
           'Application::GetInstance().SetWgCwMode(entering_cw); // '+MARKER,f)
    else:
        s = regex_once(s, single,
           'Application::GetInstance().SetWgCwMode(entering_cw); // '+MARKER,f)
    # Existing 'if (!cw_mode_ || !cw_changed) return' and key logs.
    pat=r'(if\s*\(!cw_mode_\s*\|\|\s*!cw_changed\)\s*return;\s*if\s*\(cw_key_\.stable_pressed\)\s*\{)'
    s=regex_once(s,pat,r'\1\n            Application::GetInstance().Schedule([](){ Application::GetInstance().SetWgCwKey(true); }); // WG_CW_ISOLATION_TONE_V1',f)
    pat=r'(\}\s*else\s*\{\s*const int64_t held_ms\s*=)'
    s=regex_once(s,pat,r'} else {\n            Application::GetInstance().Schedule([](){ Application::GetInstance().SetWgCwKey(false); }); // WG_CW_ISOLATION_TONE_V1\n            const int64_t held_ms =',f)
    # If GPIO17 is still held when mode changes, kill tone immediately by SetWgCwMode(false).
    return s

PATCHES={TARGETS[0]:patch_app_h,TARGETS[1]:patch_app_cc,TARGETS[2]:patch_audio_h,
         TARGETS[3]:patch_audio_cc,TARGETS[4]:patch_controller}

def sha(b): return hashlib.sha256(b).hexdigest()

def apply(root,check):
    if BACKUP.exists(): fail('Backup directory exists; rollback before re-applying')
    originals={}; modified={}
    for target in TARGETS:
        p=root/target
        if not p.is_file(): fail('Missing '+str(p))
        b=p.read_bytes(); txt=b.decode('utf-8')
        if MARKER in txt: fail(f'{target}: already patched')
        # CRLF normalization only in memory; preserve each file newline convention.
        norm=txt.replace('\r\n','\n')
        out=PATCHES[target](norm,target)
        # Preserve original EOF-newline convention; some upstream headers intentionally end without LF.
        if txt.endswith('\n') and not out.endswith('\n'):
            fail(f'{target}: patch unexpectedly removed trailing newline')
        if not txt.endswith('\n') and out.endswith('\n'):
            out=out[:-1]
        new=out.replace('\n','\r\n') if '\r\n' in txt else out
        originals[target]=b;modified[target]=new.encode('utf-8')
    if check:
        print('CHECK OK: all 5 source files match expected v2.2.6 + GPIO39 base; no files modified')
        return
    directory=root/BACKUP
    directory.mkdir(exist_ok=False)
    manifest={}
    try:
        for target,b in originals.items():
            destination=directory/target;destination.parent.mkdir(parents=True,exist_ok=True)
            destination.write_bytes(b)
            manifest[target]={'original_sha256':sha(b),'patched_sha256':sha(modified[target])}
        (directory/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
        for target,b in modified.items(): (root/target).write_bytes(b)
    except BaseException:
        for target,b in originals.items(): (root/target).write_bytes(b)
        raise
    print('APPLIED: 5 source files. Backup is '+str(directory))
    print('GPIO39 mode; GPIO17 sidetone; wake word/voice gated in CW; pins unchanged')

def rollback(root):
    directory=root/BACKUP
    if not directory.is_dir(): fail('No backup at '+str(directory))
    manifest=json.loads((directory/'manifest.json').read_text(encoding='utf-8'))
    for target in TARGETS:
        if target not in manifest: fail('Backup manifest incomplete')
        orig=(directory/target).read_bytes()
        if sha(orig)!=manifest[target]['original_sha256']:fail('Backup checksum mismatch for '+target)
        current=(root/target).read_bytes()
        if sha(current)!=manifest[target]['patched_sha256']:fail('File was modified after applying patch; refusing to overwrite '+target)
    for target in TARGETS: (root/target).write_bytes((directory/target).read_bytes())
    print('ROLLED BACK all five files from verified backups')

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    group=parser.add_mutually_exclusive_group()
    group.add_argument('--check',action='store_true',help='Validate without modifying files')
    group.add_argument('--rollback',action='store_true',help='Restore verified backups')
    args=parser.parse_args()
    root=Path.cwd()
    try:
        if args.rollback: rollback(root)
        else: apply(root,args.check)
    except Exception as e:
        print('ERROR:',e,file=sys.stderr)
        sys.exit(1)
