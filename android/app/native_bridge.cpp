#include <jni.h>
#include "touch_engine.h"
#include "initialization_probe.h"
#include <sstream>
#include <exception>
#include <vector>
#include <filesystem>
#include <fstream>
#include <mutex>
int run_bootstrap_entry_probe(std::ostream&);
static mp2r::TouchEngine input;
static std::mutex pathMutex;
static std::filesystem::path dataRoot;

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
        auto result=mp2r::runInitialization(elf.data(),elf.size(),trace,boot_path);
        bool expected=result.reason=="missing_function" && result.pc==0x42f7e0 && result.source==0x100208 && result.semaphores==3 &&
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
