#include "audio_mixer.h"
constexpr uint32_t soundMethodBase=0x7c000000;
constexpr uint32_t soundBadFormat=0x88780064;
PcmMixer soundMixer;
struct SoundObject {
    uint32_t kind,table,references=1,flags=0,guestSamples=0,volume=0,pan=0;
    uint32_t parent=0;                        // kinds 3/4: el búfer dueño
    float position[3]{},velocity[3]{},minDistance=1.f,maxDistance=1e9f,mode=0;  // DS3D
    std::shared_ptr<PcmVoice> voice;
    bool locked=false;
    uint32_t lockOffset=0,lockBytes=0;
    std::chrono::steady_clock::time_point lastPublish{};
    std::chrono::microseconds maxPublishGap{};
};
std::map<uint32_t,SoundObject> soundObjects;
std::map<uint32_t,uint32_t> soundMethodCalls;
uint32_t soundToken{};
#ifdef __ANDROID__
AAudioStream* soundStream=nullptr;
int32_t soundLastXruns=0;
std::atomic<uint32_t> soundCallbackPeakUs{0};
aaudio_data_callback_result_t soundCallback(AAudioStream*,void*,void* data,int32_t frames){
    auto start=std::chrono::steady_clock::now();
    soundMixer.render(static_cast<float*>(data),unsigned(frames));
    uint32_t us=uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());
    auto peak=soundCallbackPeakUs.load(std::memory_order_relaxed);
    while(us>peak&&!soundCallbackPeakUs.compare_exchange_weak(peak,us,std::memory_order_relaxed)){}
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
// Output buffer: 40 ms, grown to 100 ms after reported underruns. Two bursts
// (4-10 ms on MediaTek, Exynos and Unisoc low-latency streams) were all a
// render or guest-lock hiccup needed to break the sound on busy low-end
// phones, and those drivers report no xruns, so the old grow-on-xrun rule
// never fired. A racing game does not need lower latency than this.
constexpr int32_t soundTargetMs=40,soundMaximumMs=100;
AAudioStream* openSoundStream(aaudio_performance_mode_t mode){
    AAudioStreamBuilder* builder=nullptr;
    if(AAudio_createStreamBuilder(&builder)!=AAUDIO_OK)return nullptr;
    AAudioStreamBuilder_setDirection(builder,AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(builder,AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(builder,2);
    AAudioStreamBuilder_setSharingMode(builder,AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder,mode);
    AAudioStreamBuilder_setBufferCapacityInFrames(builder,48000*soundMaximumMs/1000);
    AAudioStreamBuilder_setDataCallback(builder,soundCallback,nullptr);
    AAudioStream* stream=nullptr;
    auto status=AAudioStreamBuilder_openStream(builder,&stream);AAudioStreamBuilder_delete(builder);
    return status==AAUDIO_OK?stream:nullptr;
}
int32_t soundFrames(int32_t milliseconds){
    int32_t frames=int32_t(soundMixer.rate)*milliseconds/1000,burst=AAudioStream_getFramesPerBurst(soundStream);
    if(burst>0)frames=(frames+burst-1)/burst*burst;
    return std::min(frames,AAudioStream_getBufferCapacityInFrames(soundStream));
}
bool startSoundStream(){
    if(soundStream)return true;
    soundStream=openSoundStream(AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    // Some MediaTek low-latency streams cap the buffer at two bursts (512 frames).
    if(soundStream&&AAudioStream_getBufferCapacityInFrames(soundStream)<AAudioStream_getSampleRate(soundStream)*soundTargetMs/1000){
        if(AAudioStream* fallback=openSoundStream(AAUDIO_PERFORMANCE_MODE_NONE)){AAudioStream_close(soundStream);soundStream=fallback;}
    }
    if(!soundStream)soundStream=openSoundStream(AAUDIO_PERFORMANCE_MODE_NONE);
    if(!soundStream)return false;
    soundMixer.rate=unsigned(AAudioStream_getSampleRate(soundStream));
    AAudioStream_setBufferSizeInFrames(soundStream,soundFrames(soundTargetMs));
    soundLastXruns=0;soundCallbackPeakUs=0;
    auto status=AAudioStream_requestStart(soundStream);
    if(status!=AAUDIO_OK){AAudioStream_close(soundStream);soundStream=nullptr;return false;}
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","AAudio stereo float stream started: %u Hz",soundMixer.rate);return true;
}
// Called on the guest thread after Present, never from the real-time callback.
// Grow only after an actual underrun, one burst at a time, bounded by capacity.
void serviceGuestSound(){
    if(!soundStream)return;
    int32_t xruns=AAudioStream_getXRunCount(soundStream),burst=AAudioStream_getFramesPerBurst(soundStream);
    int32_t size=AAudioStream_getBufferSizeInFrames(soundStream),capacity=AAudioStream_getBufferCapacityInFrames(soundStream);
    if(xruns>soundLastXruns&&burst>0&&size<soundFrames(soundMaximumMs))
        AAudioStream_setBufferSizeInFrames(soundStream,std::min(size+burst,soundFrames(soundMaximumMs)));
    if(xruns>=0)soundLastXruns=xruns;
    uint64_t clipped;float peak;unsigned active=0;double publishGapMs=0,ringMs=0;
    {std::lock_guard<std::mutex> lock(soundMixer.mutex);
        clipped=soundMixer.clippedSamples;peak=soundMixer.peakAmplitude;
        soundMixer.clippedSamples=0;soundMixer.peakAmplitude=0;
        for(const auto& voice:soundMixer.voices)if(voice&&voice->playing)++active;
        for(auto& item:soundObjects){auto& object=item.second;auto& voice=object.voice;
            if(voice&&voice->playing&&voice->loop){
                publishGapMs=std::max(publishGapMs,double(object.maxPublishGap.count())/1000);
                ringMs=std::max(ringMs,1000.0*voice->samples.size()/voice->block()/voice->frequency);
            }
            object.maxPublishGap={};}}
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Audio producer: maxPublishGap=%.2f ms ring=%.2f ms",publishGapMs,ringMs);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Audio mix: active=%u peak=%.4f clipped=%llu",active,double(peak),static_cast<unsigned long long>(clipped));
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Audio health: xruns=%d buffer=%d/%d burst=%d rate=%u callbackPeak=%u us mode=%d",
        xruns,AAudioStream_getBufferSizeInFrames(soundStream),capacity,burst,soundMixer.rate,
        soundCallbackPeakUs.exchange(0,std::memory_order_relaxed),int(AAudioStream_getPerformanceMode(soundStream)));
}
#else
void serviceGuestSound(){}
bool startSoundStream(){return true;} // Host tests inspect the mixer; no physical audio output.
#endif
void shutdownGuestSound(){
#ifdef __ANDROID__
    if(soundStream){AAudioStream_requestStop(soundStream);AAudioStream_close(soundStream);soundStream=nullptr;}
#endif
    for(auto& item:soundObjects){auto& object=item.second;if(object.voice)soundMixer.remove(object.voice);
        if(object.guestSamples)heaps->free(GuestHeaps::process,object.guestSamples);
        heaps->free(GuestHeaps::process,object.table);heaps->free(GuestHeaps::process,item.first);}
    soundObjects.clear();
}
uint32_t createSoundObject(uint32_t kind,uint32_t flags=0){
    uint32_t address=allocate(4),table=allocate(24*4);
    for(uint32_t slot=0;slot<24;++slot)write32(table+4*slot,soundMethodBase+kind*0x1000+slot*16);
    write32(address,table);soundObjects.emplace(address,SoundObject{kind,table,1,flags});return address;
}
uint16_t soundRead16(uint32_t address){uint16_t value;std::memcpy(&value,ptr(address,2),2);return value;}
bool soundFormat(uint32_t address,PcmVoice& voice){
    if(!address)return false;unsigned channels=soundRead16(address+2),rate=read32(address+4),bits=soundRead16(address+14);
    if(soundRead16(address)!=1||(channels!=1&&channels!=2)||(bits!=8&&bits!=16)||rate<100||rate>200000)return false;
    if(soundRead16(address+12)!=channels*(bits/8)||read32(address+8)!=rate*channels*(bits/8))return false;
    voice.channels=channels;voice.rate=voice.frequency=rate;voice.bits=bits;return true;
}
void directSoundCreate(){
    if(!arg(1)){ret(0x80004003,3);return;}write32(arg(1),0);
    if(arg(2)){ret(0x80040110,3);return;}
    if(arg(0)){ret(0x88780078,3);return;} // Only the default playback device is exposed.
    if(!startSoundStream()){ret(0x88780078,3);return;}
    write32(arg(1),createSoundObject(1));ret(0,3);
}
// DirectSound3D: atenuación por distancia y panorama respecto al oyente, aplicados como
// ganancia de la voz. Sin Doppler ni reverberación EAX: quedan declarados en docs.
float soundListener[3]{},soundListenerFront[3]{0,0,1},soundListenerTop[3]{0,1,0};
float soundFloat(uint32_t bits){float value;std::memcpy(&value,&bits,4);return value;}
void soundApplyGain(SoundObject& buffer){
    if(!buffer.voice)return;
    float gain=std::pow(10.f,int32_t(buffer.volume)/2000.f),pan=float(int32_t(buffer.pan));
    float left=gain*(pan>0?std::pow(10.f,-pan/2000.f):1),right=gain*(pan<0?std::pow(10.f,pan/2000.f):1);
    SoundObject* spatial=nullptr;
    for(auto& item:soundObjects)if(item.second.kind==3&&item.second.parent&&soundObjects.count(item.second.parent)&&&soundObjects.at(item.second.parent)==&buffer)spatial=&item.second;
    if(spatial&&spatial->mode==0){
        float d[3]={spatial->position[0]-soundListener[0],spatial->position[1]-soundListener[1],spatial->position[2]-soundListener[2]};
        float distance=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        float attenuation=distance<=spatial->minDistance?1.f:spatial->minDistance/std::min(distance,spatial->maxDistance);
        // Vector derecho del oyente: arriba x frente.
        float* f=soundListenerFront;float* u=soundListenerTop;
        float r[3]={u[1]*f[2]-u[2]*f[1],u[2]*f[0]-u[0]*f[2],u[0]*f[1]-u[1]*f[0]};
        float side=distance>1e-3f?(d[0]*r[0]+d[1]*r[1]+d[2]*r[2])/distance:0.f;
        left*=attenuation*std::sqrt(0.5f*(1.f-side));right*=attenuation*std::sqrt(0.5f*(1.f+side));
    }
    buffer.voice->left=left;buffer.voice->right=right;
}
void sound3DMethod(SoundObject& object,uint32_t kind,uint32_t method){
    auto owner=soundObjects.find(object.parent);if(owner==soundObjects.end())throw GuestStop("Objeto DirectSound3D sin búfer");
    auto vector=[&](float* out,uint32_t argc){out[0]=soundFloat(arg(1));out[1]=soundFloat(arg(2));out[2]=soundFloat(arg(3));ret(0,argc);};
    auto reapply=[&]{std::lock_guard<std::mutex> lock(soundMixer.mutex);if(kind==3)soundApplyGain(owner->second);else for(auto& item:soundObjects)if(item.second.kind==2&&item.second.voice)soundApplyGain(item.second);};
    if(kind==3)switch(method){
        case 7:std::memcpy(ptr(arg(1),4),&object.maxDistance,4);ret(0,2);return;
        case 8:std::memcpy(ptr(arg(1),4),&object.minDistance,4);ret(0,2);return;
        case 9:{uint32_t mode=uint32_t(object.mode);write32(arg(1),mode);ret(0,2);return;}
        case 10:std::memcpy(ptr(arg(1),12),object.position,12);ret(0,2);return;
        case 11:std::memcpy(ptr(arg(1),12),object.velocity,12);ret(0,2);return;
        case 12:{uint32_t p=arg(1);std::memcpy(object.position,ptr(p+4,12),12);std::memcpy(object.velocity,ptr(p+16,12),12);
            std::memcpy(&object.minDistance,ptr(p+52,4),4);std::memcpy(&object.maxDistance,ptr(p+56,4),4);object.mode=float(read32(p+60));reapply();ret(0,3);return;}
        case 13:ret(0,4);return;case 14:ret(0,5);return;case 15:ret(0,3);return;
        case 16:object.maxDistance=soundFloat(arg(1));reapply();ret(0,3);return;
        case 17:object.minDistance=soundFloat(arg(1));reapply();ret(0,3);return;
        case 18:object.mode=float(arg(1));reapply();ret(0,3);return;
        case 19:vector(object.position,5);reapply();return;
        case 20:vector(object.velocity,5);return;
        default:throw GuestStop("IDirectSound3DBuffer método pendiente: "+std::to_string(method));
    }
    switch(method){
        case 7:std::memcpy(ptr(arg(1),12),soundListener,12);ret(0,2);return;
        case 11:case 12:case 15:ret(0,3);return;  // Factores de distancia, Doppler y atenuación: valor por defecto.
        case 10:{uint32_t p=arg(1);std::memcpy(soundListener,ptr(p+4,12),12);std::memcpy(soundListenerFront,ptr(p+40,12),12);std::memcpy(soundListenerTop,ptr(p+52,12),12);reapply();ret(0,3);return;}
        case 13:for(int i=0;i<3;++i){soundListenerFront[i]=soundFloat(arg(1+i));soundListenerTop[i]=soundFloat(arg(4+i));}reapply();ret(0,8);return;
        case 14:vector(soundListener,5);reapply();return;
        case 16:ret(0,5);return;
        case 17:ret(0,1);return;  // CommitDeferredSettings: todo se aplica al momento.
        default:throw GuestStop("IDirectSound3DListener método pendiente: "+std::to_string(method));
    }
}
void soundDispatch(){
    uint32_t kind=(soundToken-soundMethodBase)/0x1000,method=((soundToken-soundMethodBase)%0x1000)/16,self=arg(0);
    auto found=soundObjects.find(self);if(found==soundObjects.end()||found->second.kind!=kind)throw GuestStop("Invalid DirectSound object");
    auto& object=found->second;
    if(++soundMethodCalls[soundToken]<=3)NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","DirectSound kind=%u method=%u",kind,method);
    if(method==0){
        if(!arg(2)){ret(0x80004003,3);return;}write32(arg(2),0);
        const std::array<uint8_t,16> dsGuid={0x83,0xfa,0x9a,0x27,0x81,0x49,0xce,0x11,0xa5,0x21,0,0x20,0xaf,0x0b,0xe5,0x60};
        const std::array<uint8_t,16> bufferGuid={0x85,0xfa,0x9a,0x27,0x81,0x49,0xce,0x11,0xa5,0x21,0,0x20,0xaf,0x0b,0xe5,0x60};
        const std::array<uint8_t,16> unknown={0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
        const std::array<uint8_t,16> listenerGuid={0x84,0xfa,0x9a,0x27,0x81,0x49,0xce,0x11,0xa5,0x21,0,0x20,0xaf,0x0b,0xe5,0x60};
        const std::array<uint8_t,16> buffer3dGuid={0x86,0xfa,0x9a,0x27,0x81,0x49,0xce,0x11,0xa5,0x21,0,0x20,0xaf,0x0b,0xe5,0x60};
        if(kind==2&&(inputGuidEquals(arg(1),listenerGuid)||inputGuidEquals(arg(1),buffer3dGuid))){
            uint32_t child=createSoundObject(inputGuidEquals(arg(1),listenerGuid)?4:3);
            soundObjects.at(child).parent=self;write32(arg(2),child);ret(0,3);return;
        }
        if(!inputGuidEquals(arg(1),kind==1?dsGuid:bufferGuid)&&!inputGuidEquals(arg(1),unknown)){
            static unsigned logged=0;if(++logged<=16){uint32_t data1=read32(arg(1));NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","DirectSound QI sin soporte kind=%u iid=%08x",kind,data1);}
            ret(0x80004002,3);return;
        }
        ++object.references;write32(arg(2),self);ret(0,3);return;
    }
    if(method==1){ret(++object.references,1);return;}
    if(method==2){uint32_t refs=--object.references;if(!refs){if(object.voice)soundMixer.remove(object.voice);if(object.guestSamples)heaps->free(GuestHeaps::process,object.guestSamples);heaps->free(GuestHeaps::process,object.table);heaps->free(GuestHeaps::process,self);soundObjects.erase(found);}ret(refs,1);return;}
    if(kind==3||kind==4){sound3DMethod(object,kind,method);return;}
    if(kind==1){switch(method){
        case 3:{uint32_t desc=arg(1),output=arg(2);if(!output){ret(0x80004003,4);return;}write32(output,0);
            if(!desc||(read32(desc)!=20&&read32(desc)!=36)||arg(3)){ret(0x80070057,4);return;}
            uint32_t flags=read32(desc+4),bytes=read32(desc+8),format=read32(desc+16);
            auto voice=std::make_shared<PcmVoice>();
            if(!(flags&1)&&(!soundFormat(format,*voice)||!bytes||bytes>16*1024*1024||bytes%voice->block())){ret(soundBadFormat,4);return;}
            if(!(flags&1))NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","DirectSound buffer %u Hz %u ch %u bit, %u bytes, flags %08x",voice->rate,voice->channels,voice->bits,bytes,flags);
            if(!(flags&1)){voice->samples.resize(bytes,voice->bits==8?128:0);if(!soundMixer.add(voice)){ret(0x8878000a,4);return;}}
            uint32_t buffer=createSoundObject(2,flags);auto& created=soundObjects.at(buffer);created.voice=voice;
            if(!(flags&1)){created.guestSamples=allocate(bytes);std::memcpy(ptr(created.guestSamples,bytes),voice->samples.data(),bytes);}
            write32(output,buffer);ret(0,4);return;}
        case 4:{uint32_t out=arg(1);if(!out||read32(out)!=96){ret(0x80070057,2);return;}std::memset(ptr(out+4,92),0,92);
            // Match the PCM formats and continuous resampling we implement. The old
            // 0x147 omitted PRIMARY16BIT, SECONDARY16BIT and SECONDARYSTEREO.
            // A Windows DirectSound probe reports 0xF1F for the same formats.
            write32(out+4,0xf1f);write32(out+8,100);write32(out+12,200000);write32(out+16,1);ret(0,2);return;}
        case 5:{uint32_t source=arg(1),output=arg(2);auto original=soundObjects.find(source);
            if(original==soundObjects.end()||!original->second.voice||!output){ret(0x80070057,3);return;}
            auto voice=std::make_shared<PcmVoice>(*original->second.voice);voice->playing=false;voice->position=0;
            if(!soundMixer.add(voice)){ret(0x8878000a,3);return;}
            uint32_t duplicate=createSoundObject(2,original->second.flags);auto& created=soundObjects.at(duplicate);created.voice=voice;
            uint32_t bytes=uint32_t(voice->samples.size());created.guestSamples=allocate(bytes);
            std::memcpy(ptr(created.guestSamples,bytes),voice->samples.data(),bytes);write32(output,duplicate);ret(0,3);return;}
        case 6:if(!windows.count(arg(1))||arg(2)<1||arg(2)>4){ret(0x80070057,3);return;}ret(0,3);return;
        case 7:ret(0,1);return;  // Compact
        case 9:ret(0,2);return;  // SetSpeakerConfig
        case 10:ret(0,2);return; // Initialize
        case 8:if(!arg(1)){ret(0x80004003,2);return;}write32(arg(1),4);ret(0,2);return; // Stereo speaker configuration.
        default:throw GuestStop("DirectSound interface method pending: "+std::to_string(method));
    }}
    auto voice=object.voice;if(!voice)throw GuestStop("DirectSound buffer missing PCM voice");
    switch(method){
    case 3:{uint32_t out=arg(1);if(!out||read32(out)!=20){ret(0x80070057,2);return;}write32(out+4,(object.flags&~4u)|8u);write32(out+8,uint32_t(voice->samples.size()));write32(out+12,0);write32(out+16,0);ret(0,2);return;}
    case 4:{std::lock_guard<std::mutex> lock(soundMixer.mutex);uint32_t bytes=uint32_t(voice->samples.size()),position=uint32_t(voice->position)*voice->block();
        if(arg(1))write32(arg(1),position);if(arg(2))write32(arg(2),bytes?(position+soundMixer.rate/100*voice->block())%bytes:0);ret(0,3);return;}
    case 5:{uint32_t format=arg(1),capacity=arg(2),written=arg(3);
        if(written)write32(written,18);
        if(format&&capacity>=16){uint16_t block=uint16_t(voice->block());uint32_t words[4]={1u|(uint32_t(voice->channels)<<16),voice->rate,voice->rate*block,uint32_t(block)|(uint32_t(voice->bits)<<16)};
            std::memcpy(ptr(format,16),words,16);if(capacity>=18){uint16_t zero=0;std::memcpy(ptr(format+16,2),&zero,2);}}
        ret(0,4);return;}
    case 10:ret(0,3);return;  // Initialize
    case 6:case 7:case 8:case 9:{if(!arg(1)){ret(0x80004003,2);return;}
        std::lock_guard<std::mutex> lock(soundMixer.mutex);write32(arg(1),method==6?object.volume:method==7?object.pan:method==8?voice->frequency:(voice->playing?1u|(voice->loop?4u:0u):0u));ret(0,2);return;}
    case 11:{uint32_t bytes=uint32_t(voice->samples.size()),offset=arg(1),length=arg(2),flags=arg(7);
        if(!bytes||object.locked||flags&~3u||!arg(3)||!arg(4)){ret(0x80070057,8);return;}
        if(flags&1){std::lock_guard<std::mutex> lock(soundMixer.mutex);offset=(uint32_t(voice->position)*voice->block()+soundMixer.rate/100*voice->block())%bytes;}
        if(flags&2){offset=0;length=bytes;}
        if(offset>=bytes||length>bytes){ret(0x80070057,8);return;}uint32_t first=std::min(length,bytes-offset),second=length-first;
        if(second&&(!arg(5)||!arg(6))){ret(0x80070057,8);return;}
        object.locked=true;object.lockOffset=offset;object.lockBytes=length;
        write32(arg(3),object.guestSamples+offset);write32(arg(4),first);if(arg(5))write32(arg(5),second?object.guestSamples:0);if(arg(6))write32(arg(6),second);ret(0,8);return;}
    case 12:{if(arg(1)||arg(2)||arg(3)&~1u){ret(0x80070057,4);return;}std::lock_guard<std::mutex> lock(soundMixer.mutex);voice->loop=arg(3)&1;voice->playing=true;voice->staleFrames=0;ret(0,4);return;}
    case 13:{if(arg(1)>=voice->samples.size()||arg(1)%voice->block()){ret(0x80070057,2);return;}std::lock_guard<std::mutex> lock(soundMixer.mutex);voice->position=arg(1)/voice->block();voice->staleFrames=0;ret(0,2);return;}
    case 14:{if(!(object.flags&1)){ret(0x80070057,2);return;}std::lock_guard<std::mutex> lock(soundMixer.mutex);ret(soundFormat(arg(1),*voice)?0:soundBadFormat,2);return;}
    case 15:case 16:{int32_t value=int32_t(arg(1));if(value < -10000||value>(method==15?0:10000)){ret(0x80070057,2);return;}
        std::lock_guard<std::mutex> lock(soundMixer.mutex);if(method==15)object.volume=arg(1);else object.pan=arg(1);
        soundApplyGain(object);ret(0,2);return;}
    case 17:{uint32_t frequency=arg(1)?arg(1):voice->rate;if(frequency<100||frequency>200000){ret(0x80070057,2);return;}std::lock_guard<std::mutex> lock(soundMixer.mutex);voice->frequency=frequency;ret(0,2);return;}
    case 18:{std::lock_guard<std::mutex> lock(soundMixer.mutex);voice->playing=false;ret(0,1);return;}
    case 19:{uint32_t bytes=uint32_t(voice->samples.size()),first=std::min(object.lockBytes,bytes-object.lockOffset),second=object.lockBytes-first;
        if(!object.locked||arg(1)!=object.guestSamples+object.lockOffset||arg(2)!=first||arg(3)!=(second?object.guestSamples:0)||arg(4)!=second){ret(0x80070057,5);return;}
        // Publish only what the guest locked. Copying the whole ring under the
        // mixer lock made every Unlock stall the AAudio callback and could
        // overwrite samples still being played with a stale copy.
        {std::lock_guard<std::mutex> lock(soundMixer.mutex);
            std::memcpy(voice->samples.data()+object.lockOffset,ptr(object.guestSamples+object.lockOffset,first),first);
            if(second)std::memcpy(voice->samples.data(),ptr(object.guestSamples,second),second);
            voice->published();}
        auto published=std::chrono::steady_clock::now();
        if(object.lastPublish!=std::chrono::steady_clock::time_point{})object.maxPublishGap=std::max(object.maxPublishGap,
            std::chrono::duration_cast<std::chrono::microseconds>(published-object.lastPublish));
        object.lastPublish=published;
        object.locked=false;ret(0,5);return;}
    case 20:ret(0,1);return; // Software-owned buffers cannot be lost by the driver.
    default:throw GuestStop("DirectSound buffer method pending: "+std::to_string(method));
    }
}
