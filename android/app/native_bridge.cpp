#include <jni.h>
#include "touch_engine.h"
#include "initialization_probe.h"
#include "audio_sink.h"
#include <sstream>
#include <exception>
#include <vector>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <android/native_window_jni.h>
#include <algorithm>
#include <cmath>
int run_bootstrap_entry_probe(std::ostream&);
static mp2r::TouchEngine input;
static std::mutex pathMutex;
static std::filesystem::path dataRoot;
static std::mutex surfaceMutex,sessionMutex;
static std::condition_variable sessionWake;
static ANativeWindow* gameWindow{};
static std::atomic<bool> stopping{false},paused{false},running{false},imageFlipped{false};
static std::atomic<unsigned> menuButtons{0};
static std::atomic<uint64_t> presentedFrames{0};
static std::atomic<uint64_t> padSamples{0},pressedSamples{0};
extern "C" JNIEXPORT jboolean JNICALL
Java_com_mp2r_android_NativeBridge_prepareGame(JNIEnv*,jclass) {
    if(running) return JNI_FALSE;
    stopping=false; paused=false; imageFlipped=false; return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_mp2r_android_NativeBridge_setImageFlipped(JNIEnv*,jclass,jboolean value) { imageFlipped=value; }

extern "C" JNIEXPORT void JNICALL
Java_com_mp2r_android_NativeBridge_setSurface(JNIEnv* env,jclass,jobject surface) {
    std::scoped_lock lock(surfaceMutex);
    if(gameWindow) ANativeWindow_release(gameWindow);
    gameWindow=surface?ANativeWindow_fromSurface(env,surface):nullptr;
}
extern "C" JNIEXPORT void JNICALL
Java_com_mp2r_android_NativeBridge_setPaused(JNIEnv*,jclass,jboolean value) {
    paused=value; if(value) { input.cancel(); menuButtons=0; } sessionWake.notify_all();
}
extern "C" JNIEXPORT void JNICALL
Java_com_mp2r_android_NativeBridge_stopGame(JNIEnv*,jclass) { stopping=true; sessionWake.notify_all(); }
extern "C" JNIEXPORT void JNICALL
Java_com_mp2r_android_NativeBridge_menuInput(JNIEnv*,jclass,jint bits) { menuButtons=unsigned(bits)&0xffff; }
extern "C" JNIEXPORT jlong JNICALL
Java_com_mp2r_android_NativeBridge_frameCount(JNIEnv*,jclass) { return jlong(presentedFrames.load()); }

extern "C" JNIEXPORT jstring JNICALL
Java_com_mp2r_android_NativeBridge_runGame(JNIEnv* env,jclass,jbyteArray original,jstring boot,jstring logPath,jboolean profiling) {
    if(running.exchange(true)) return env->NewStringUTF("Ya hay una sesión activa");
    struct End { ~End(){ running=false; input.cancel(); menuButtons=0; } } end;
    try {
        presentedFrames=0; padSamples=0; pressedSamples=0;
        if(!original || env->GetArrayLength(original)!=4984624 || !boot || !logPath) throw std::runtime_error("Datos PAL incompletos");
        std::vector<uint8_t> elf(4984624);
        env->GetByteArrayRegion(original,0,jsize(elf.size()),reinterpret_cast<jbyte*>(elf.data()));
        if(env->ExceptionCheck()) return nullptr;
        auto string=[&](jstring value){ const char* p=env->GetStringUTFChars(value,nullptr); if(!p) throw std::runtime_error("JNI string"); std::string s(p); env->ReleaseStringUTFChars(value,p); return s; };
        auto bootPath=string(boot),logFile=string(logPath);
        std::filesystem::path root; { std::scoped_lock lock(pathMutex); root=dataRoot; }
        if(root.empty()) throw std::runtime_error("Carpeta de datos sin configurar");
        std::ofstream log(logFile); if(!log) throw std::runtime_error("No se pudo abrir el registro");
        AndroidAudioSink audio(paused,stopping); bool audio_open=audio.open();
        log<<"AUDIO AAudio PCM16 stereo 48000 open="<<audio_open<<'\n';
        mp2r::RuntimeCallbacks callbacks;
        callbacks.profile=profiling;
        callbacks.compensate_scene_preview=true;
        if(audio_open) callbacks.play_audio=[&](const int16_t* pcm,uint32_t frames){ audio.enqueue(pcm,frames); };
        callbacks.should_stop=[] {
            if(paused && !stopping) { std::unique_lock lock(sessionMutex); sessionWake.wait(lock,[]{ return !paused || stopping; }); }
            return stopping.load();
        };
        callbacks.poll_pad=[] {
            auto state=input.consume(); mp2r::GamePadInput pad;
            pad.pressed=uint16_t(menuButtons.load());
            // Max Payne 2 PS2 manual defaults: R1, R3, Triangle, Cross,
            // Square, L2, L1, D-pad Up (weapons), Circle, Start.
            static constexpr uint16_t bits[12]={0,0,0x800,0x4,0x1000,0x4000,0x8000,0x100,0x400,0x10,0x2000,0x8};
            for(unsigned n=2;n<12;++n) if(state[n].held) pad.pressed|=bits[n];
            if(state[7].held && state[0].x*state[0].x+state[0].y*state[0].y>.04f)
                pad.pressed=uint16_t((pad.pressed&~0x100u)|0x400u); // moving + Bullet Time = Shootdodge
            auto axis=[](float x){ return uint8_t(std::clamp(int(std::lround(128+127*x)),0,255)); };
            pad.sticks={axis(state[1].x),axis(state[1].y),axis(state[0].x),axis(state[0].y)};
            static constexpr uint16_t pressureBits[]={0x20,0x80,0x10,0x40,0x1000,0x2000,0x4000,0x8000,0x400,0x800,0x100,0x200};
            for(unsigned n=0;n<12;++n) if(pad.pressed&pressureBits[n]) pad.pressure[n]=255;
            ++padSamples; if(pad.pressed) ++pressedSamples;
            return pad;
        };
        callbacks.present_frame=[](const uint32_t* pixels,uint32_t w,uint32_t h) {
            std::scoped_lock lock(surfaceMutex); if(!gameWindow) return;
            if(ANativeWindow_getWidth(gameWindow)!=int(w) || ANativeWindow_getHeight(gameWindow)!=int(h) || ANativeWindow_getFormat(gameWindow)!=WINDOW_FORMAT_RGBA_8888)
                if(ANativeWindow_setBuffersGeometry(gameWindow,int(w),int(h),WINDOW_FORMAT_RGBA_8888)!=0) return;
            ANativeWindow_Buffer buffer{};
            if(ANativeWindow_lock(gameWindow,&buffer,nullptr)!=0) return;
            if(buffer.format==WINDOW_FORMAT_RGBA_8888 && buffer.width==int(w) && buffer.height==int(h)) {
                const bool flipped=imageFlipped.load();
                for(uint32_t y=0;y<h;++y) std::memcpy(static_cast<uint32_t*>(buffer.bits)+size_t(y)*buffer.stride,pixels+size_t(flipped?h-1-y:y)*w,size_t(w)*4);
                ++presentedFrames;
            }
            ANativeWindow_unlockAndPost(gameWindow);
        };
        auto result=mp2r::runInitialization(elf.data(),elf.size(),log,bootPath,0x60000,4000000000u,UINT64_MAX,root.string(),callbacks);
        log<<"RESULT reason="<<result.reason<<" frames="<<presentedFrames.load()<<" steps="<<result.functions
           <<" pad_samples="<<padSamples.load()<<" pressed_samples="<<pressedSamples.load()<<std::endl;
        log<<"AUDIO queued_frames="<<audio.queued_frames.load()<<" played_frames="<<audio.played_frames.load()
           <<" dropped_frames="<<audio.dropped_frames.load()<<" nonzero_samples="<<audio.nonzero_samples.load()<<std::endl;
        std::ostringstream status; status<<result.reason<<" · PC 0x"<<std::hex<<result.pc;
        return env->NewStringUTF(status.str().c_str());
    } catch(const std::exception& error) { return env->NewStringUTF(error.what()); }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mp2r_android_NativeBridge_runInitialization(JNIEnv* env,jclass,jbyteArray original,jstring boot) {
    try {
        if(!original || env->GetArrayLength(original)!=4984624) throw std::runtime_error("Unexpected PAL ELF byte count");
        if(!boot) throw std::runtime_error("Missing SYSTEM.CNF boot path");
        std::vector<uint8_t> elf(4984624);
        env->GetByteArrayRegion(original,0,static_cast<jsize>(elf.size()),reinterpret_cast<jbyte*>(elf.data()));
        if(env->ExceptionCheck()) return nullptr;
        const char* text=env->GetStringUTFChars(boot,nullptr); if(!text) return nullptr;
        std::string boot_path(text); env->ReleaseStringUTFChars(boot,text);
        std::ostringstream trace,report;
        mp2r::RuntimeCallbacks callbacks; callbacks.break_pc=0x42f7e0;
        auto result=mp2r::runInitialization(elf.data(),elf.size(),trace,boot_path,0x60000,2000000,200000,{},callbacks);
        bool expected=result.reason=="breakpoint" && result.pc==0x42f7e0 && result.source==0x100208 && result.semaphores==3 &&
            result.alarm_patch_installed && result.tlb_services_installed && result.main_entry_verified &&
            result.threads==2 && result.context_switches==2 && result.semaphore_waits==1;
        report<<(expected?"PASS: ":"FAIL: ")<<"original InitSys pc=0x"<<std::hex<<result.pc<<" address=0x"<<result.memory_address
              <<std::dec<<" semaphores="<<result.semaphores<<" syscalls="<<result.syscalls<<" alarm_patch="<<result.alarm_patch_installed
              <<" tlb_services="<<result.tlb_services_installed<<" main_entry="<<result.main_entry_verified
              <<" threads="<<result.threads<<" switches="<<result.context_switches<<" waits="<<result.semaphore_waits
              <<" reason="<<result.reason<<". No gameplay.\n"<<trace.str();
        return env->NewStringUTF(report.str().c_str());
    } catch(const std::exception& error) {
        std::string message="FAIL: "+std::string(error.what()); return env->NewStringUTF(message.c_str());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_mp2r_android_NativeBridge_configureDataDirectory(JNIEnv* env,jclass,jstring value) {
    const char* text=env->GetStringUTFChars(value,nullptr); if(!text) return;
    std::string path(text); env->ReleaseStringUTFChars(value,text);
    std::scoped_lock lock(pathMutex); dataRoot=path;
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_mp2r_android_NativeBridge_inspectExecutable(JNIEnv* env,jclass,jstring value) {
    const char* text=env->GetStringUTFChars(value,nullptr); if(!text) return nullptr;
    std::string relative(text); env->ReleaseStringUTFChars(value,text);
    try {
        std::filesystem::path root;
        { std::scoped_lock lock(pathMutex); root=dataRoot; }
        auto target=std::filesystem::weakly_canonical(root/relative);
        auto base=std::filesystem::weakly_canonical(root);
        auto child=target.lexically_relative(base);
        if(root.empty() || child.empty() || child.is_absolute() || *child.begin()=="..") throw std::runtime_error("Invalid data path");
        std::ifstream file(target,std::ios::binary); unsigned char h[52]{};
        file.read(reinterpret_cast<char*>(h),sizeof h);
        if(!file || h[0]!=0x7f || h[1]!='E' || h[2]!='L' || h[3]!='F' || h[4]!=1 || h[5]!=1 || h[18]!=8) throw std::runtime_error("Invalid PS2 ELF");
        uint32_t entry=uint32_t(h[24])|(uint32_t(h[25])<<8)|(uint32_t(h[26])<<16)|(uint32_t(h[27])<<24);
        std::ostringstream status; status<<"lectura nativa OK, entrada 0x"<<std::hex<<entry;
        return env->NewStringUTF(status.str().c_str());
    } catch(const std::exception& e) { return env->NewStringUTF(e.what()); }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mp2r_android_NativeBridge_runProbe(JNIEnv* env,jclass) {
    try {
        std::ostringstream result;
        run_bootstrap_entry_probe(result);
        return env->NewStringUTF(result.str().c_str());
    } catch(const std::exception& e) { return env->NewStringUTF(e.what()); }
}
extern "C" JNIEXPORT void JNICALL
Java_com_mp2r_android_NativeBridge_cancel(JNIEnv*,jclass) { input.cancel(); }

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_mp2r_android_NativeBridge_touchBatch(JNIEnv* env,jclass,jintArray ids,jintArray types,
    jintArray actions,jfloatArray xs,jfloatArray ys) {
    try {
        const auto n=env->GetArrayLength(ids);
        if(n>20 || env->GetArrayLength(types)!=n || env->GetArrayLength(actions)!=n ||
            env->GetArrayLength(xs)!=n || env->GetArrayLength(ys)!=n) throw std::invalid_argument("Invalid touch batch");
        std::vector<jint> p(n),t(n),a(n); std::vector<jfloat> x(n),y(n);
        env->GetIntArrayRegion(ids,0,n,p.data()); env->GetIntArrayRegion(types,0,n,t.data());
        env->GetIntArrayRegion(actions,0,n,a.data()); env->GetFloatArrayRegion(xs,0,n,x.data());
        env->GetFloatArrayRegion(ys,0,n,y.data());
        if(env->ExceptionCheck()) return nullptr;
        for(int i=0;i<n;++i) {
            switch(t[i]) {
                case 0: input.down(p[i],static_cast<mp2r::Action>(a[i]),x[i],y[i]); break;
                case 1: input.move(p[i],x[i],y[i]); break;
                case 2: input.up(p[i]); break;
                default: throw std::invalid_argument("Invalid touch type");
            }
        }
        auto state=input.consume();
        std::vector<float> values; values.reserve(state.size()*5);
        for(auto s:state) { values.insert(values.end(),{s.x,s.y,float(s.held),float(s.pressed),float(s.released)}); }
        auto result=env->NewFloatArray(static_cast<jsize>(values.size()));
        if(result) env->SetFloatArrayRegion(result,0,static_cast<jsize>(values.size()),values.data());
        return result;
    } catch(const std::exception& e) {
        input.cancel(); env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"),e.what()); return nullptr;
    }
}
