#pragma once
#include <aaudio/AAudio.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <thread>
class AndroidAudioSink {
    static constexpr uint64_t capacity=16384; // bounded preview queue, under 350 ms at 48 kHz
    std::array<int16_t,capacity*2> samples_{};
    std::atomic<uint64_t> read_{0},write_{0};
    const std::atomic<bool>& paused_;
    const std::atomic<bool>& stopping_;
    AAudioStream* stream_{};
    static aaudio_data_callback_result_t render(AAudioStream*,void* user,void* data,int32_t frames) {
        auto& sink=*static_cast<AndroidAudioSink*>(user);
        auto* out=static_cast<int16_t*>(data); std::memset(out,0,size_t(frames)*4);
        if(sink.paused_) return AAUDIO_CALLBACK_RESULT_CONTINUE;
        uint64_t head=sink.read_.load(std::memory_order_relaxed),tail=sink.write_.load(std::memory_order_acquire);
        uint64_t count=std::min<uint64_t>(uint32_t(frames),tail-head);
        for(uint64_t n=0;n<count;++n) {
            auto at=(head+n)%capacity; out[n*2]=sink.samples_[at*2]; out[n*2+1]=sink.samples_[at*2+1];
        }
        sink.read_.store(head+count,std::memory_order_release);
        sink.played_frames+=count;
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
public:
    std::atomic<uint64_t> played_frames{0},queued_frames{0},dropped_frames{0},nonzero_samples{0};
    AndroidAudioSink(const std::atomic<bool>& paused,const std::atomic<bool>& stopping):paused_(paused),stopping_(stopping){}
    bool open() {
        AAudioStreamBuilder* builder{};
        if(AAudio_createStreamBuilder(&builder)!=AAUDIO_OK) return false;
        AAudioStreamBuilder_setDirection(builder,AAUDIO_DIRECTION_OUTPUT);
        AAudioStreamBuilder_setSampleRate(builder,48000); AAudioStreamBuilder_setChannelCount(builder,2);
        AAudioStreamBuilder_setFormat(builder,AAUDIO_FORMAT_PCM_I16);
        AAudioStreamBuilder_setPerformanceMode(builder,AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
#if __ANDROID_API__ >= 28
        AAudioStreamBuilder_setUsage(builder,AAUDIO_USAGE_GAME);
#endif
        AAudioStreamBuilder_setDataCallback(builder,render,this);
        auto result=AAudioStreamBuilder_openStream(builder,&stream_); AAudioStreamBuilder_delete(builder);
        if(result!=AAUDIO_OK) { stream_=nullptr; return false; }
        if(AAudioStream_getSampleRate(stream_)!=48000 || AAudioStream_getChannelCount(stream_)!=2 || AAudioStream_getFormat(stream_)!=AAUDIO_FORMAT_PCM_I16) {
            AAudioStream_close(stream_); stream_=nullptr; return false;
        }
        AAudioStream_setBufferSizeInFrames(stream_,3*AAudioStream_getFramesPerBurst(stream_));
        if(AAudioStream_requestStart(stream_)!=AAUDIO_OK) { AAudioStream_close(stream_); stream_=nullptr; return false; }
        return true;
    }
    void enqueue(const int16_t* source,uint32_t frames) {
        uint64_t tail=write_.load(std::memory_order_relaxed),head=read_.load(std::memory_order_acquire);
        if(frames>capacity) { dropped_frames+=frames; return; }
        auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(200);
        while(frames>capacity-(tail-head)) {
            if(paused_ || stopping_ || std::chrono::steady_clock::now()>=deadline || AAudioStream_getState(stream_)==AAUDIO_STREAM_STATE_DISCONNECTED) {
                dropped_frames+=frames; return;
            }
            // The physical 48 kHz output clocks the stream; don't discard valid
            // movie samples when emulated work briefly runs ahead of real time.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            head=read_.load(std::memory_order_acquire);
        }
        uint64_t nonzero=0;
        for(uint64_t n=0;n<frames;++n) {
            auto at=(tail+n)%capacity; samples_[at*2]=source[n*2]; samples_[at*2+1]=source[n*2+1];
            nonzero+=(source[n*2]!=0)+(source[n*2+1]!=0);
        }
        nonzero_samples+=nonzero; queued_frames+=frames;
        write_.store(tail+frames,std::memory_order_release);
    }
    ~AndroidAudioSink(){ if(stream_) { AAudioStream_requestStop(stream_); AAudioStream_close(stream_); } }
};
