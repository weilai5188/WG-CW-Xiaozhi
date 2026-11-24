#pragma once
// WG_CW_LIVE_UI_V1. Simple straight-key Morse decoder; runs in existing 10-ms poll.
#include <cstdint>
#include <string>
#include "wg_cw_live_ui.h"

class WgCwDecoder {
public:
    void Reset() { pressed_=false; last_ms_=0; last_release_us_=0; finalized_=false;
                   word_space_added_=false; current_.clear(); morse_.clear(); text_.clear(); }
    void Down() { pressed_=true; finalized_=false; word_space_added_=false; }
    void Up(uint32_t held_ms, int64_t now_us) {
        pressed_=false;
        last_ms_=held_ms;
        current_ += (held_ms <= kDotDashMs ? '.' : '-');
        last_release_us_=now_us;
        finalized_=false;
    }
    // Returns true only when a character / word was finalized.
    bool Tick(int64_t now_us) {
        if (pressed_ || !last_release_us_) return false;
        const int64_t gap=(now_us-last_release_us_)/1000;
        bool changed=false;
        if (!finalized_ && !current_.empty() && gap>=kCharacterGapMs) {
            const char c=Lookup(current_);
            if (text_.size()>=30) text_.erase(0,1);
            text_ += c;
            if (!morse_.empty()) morse_ += ' ';
            morse_ += current_;
            if (morse_.size()>28) morse_.erase(0,morse_.size()-28);
            current_.clear(); finalized_=true; changed=true;
        }
        if (finalized_ && !word_space_added_ && gap>=kWordGapMs && !text_.empty()) {
            if (text_.back()!=' ') text_ += ' ';
            word_space_added_=true; changed=true;
        }
        return changed;
    }
    WgCwLiveUi::Snapshot Snapshot() const {
        WgCwLiveUi::Snapshot s;
        s.pressed=pressed_; s.last_ms=last_ms_; s.decoded=text_;
        s.current=current_; s.morse=morse_;
        if (!current_.empty()) {
            if (!s.morse.empty()) s.morse += ' ';
            s.morse += current_;
        }
        return s;
    }
private:
    static constexpr unsigned kDotDashMs=180;
    static constexpr int64_t kCharacterGapMs=350;
    static constexpr int64_t kWordGapMs=900;
    bool pressed_=false, finalized_=false, word_space_added_=false;
    unsigned last_ms_=0;
    int64_t last_release_us_=0;
    std::string current_, morse_, text_;
    static char Lookup(const std::string& s) {
        struct Entry { const char* code; char letter; };
        static constexpr Entry entries[]={
            {".-",'A'},{"-...",'B'},{"-.-.",'C'},{"-..",'D'},{".",'E'},
            {"..-.",'F'},{"--.",'G'},{"....",'H'},{"..",'I'},{".---",'J'},
            {"-.-",'K'},{".-..",'L'},{"--",'M'},{"-.",'N'},{"---",'O'},
            {".--.",'P'},{"--.-",'Q'},{".-.",'R'},{"...",'S'},{"-",'T'},
            {"..-",'U'},{"...-",'V'},{".--",'W'},{"-..-",'X'},{"-.--",'Y'},
            {"--..",'Z'},{"-----",'0'},{".----",'1'},{"..---",'2'},
            {"...--",'3'},{"....-",'4'},{".....",'5'},{"-....",'6'},
            {"--...",'7'},{"---..",'8'},{"----.",'9'},
            {".-.-.-",'.'},{"--..--",','},{"..--..",'?'},{"-..-.",'/'},
            {"-....-",'-'},{"-...-",'='},{".----.",'\''}
        };
        for (const auto& e: entries) if (s==e.code) return e.letter;
        return '?';
    }
};
