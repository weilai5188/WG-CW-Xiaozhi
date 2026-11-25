#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <cmath>
#include <cstdint>
// Independent sample-clock receiver training engine. No GPIO or USB calls.
class WgCwTraining {
public:
    std::atomic<int> tone{600}, volume{100}, wpm{20};
    std::atomic<bool> active{false};
    std::atomic<unsigned> finished{0};
    bool Start(const std::string& codes, int noise, int qrm, int qsb, int drift) {
        if(codes.empty() || codes.size()>3500) return false;
        for(char c:codes) if(c!='.'&&c!='-'&&c!=' '&&c!='/') return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if(active.load()) return false;
        codes_=codes; pos_=0; left_=0; gap_=false; mark_=false; env_=0; phase_=0; qphase_=0; clock_=0;
        noise_=noise; qrm_=qrm; qsb_=qsb; drift_=drift; active.store(true); return true;
    }
    void Stop() { if(active.exchange(false)) finished.fetch_add(1); }
    // Called only from the existing PCM task. Manual key preempts training.
    bool Render(int16_t* dst, size_t n, int rate) {
        if(!active.load()) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if(!active.load()) return false;
        constexpr float pi2=6.28318530718f;
        for(size_t i=0;i<n;i++) {
            if(!active.load()) {dst[i]=0;continue;}
            if(left_<=0) {
                int units=0;
                if(gap_) {gap_=false;mark_=false;units=1;}
                else if(pos_>=codes_.size()) {active.store(false);finished.fetch_add(1);dst[i]=0;continue;}
                else {
                    char c=codes_[pos_++]; mark_=c=='.'||c=='-';
                    if(mark_) {units=c=='-'?3:1; gap_=pos_<codes_.size()&&(codes_[pos_]=='.'||codes_[pos_]=='-');}
                    else {units=c=='/'?7:3;}
                }
                left_=static_cast<int>(rate*1.2f*units/wpm.load()+0.5f);
            }
            --left_; clock_+=1.0/rate; if(clock_>1000)clock_=0;
            float time=static_cast<float>(clock_);
            float ramp=1000.0f/rate; env_=mark_?fminf(1,env_+ramp):fmaxf(0,env_-ramp);
            phase_+=pi2*(tone.load()+drift_*0.6f*sinf(time*0.7f))/rate;
            if(phase_>=pi2)phase_-=pi2;
            qphase_+=pi2*(tone.load()+180)/rate;if(qphase_>=pi2)qphase_-=pi2;
            rng_^=rng_<<13;rng_^=rng_>>17;rng_^=rng_<<5;
            float noise=(static_cast<float>(rng_&65535)/32768.0f-1.0f)*noise_/100.0f;
            float fading=1-qsb_/100.0f*(0.5f+0.5f*sinf(time*1.9f));
            // Independent irregular keyed interfering carrier, not a second report.
            float other=(static_cast<int>(time*13)%11<5)?sinf(qphase_)*qrm_/100.0f:0;
            dst[i]=static_cast<int16_t>((sinf(phase_)*env_*fading+noise*0.8f+other*0.65f)*5000.0f*volume.load()/100.0f);
        }
        return true;
    }
private:
    std::mutex mutex_; std::string codes_; size_t pos_=0; int left_=0; bool gap_=false,mark_=false;
    int noise_=0,qrm_=0,qsb_=0,drift_=0; float env_=0,phase_=0,qphase_=0; double clock_=0; uint32_t rng_=0x12345678;
};
