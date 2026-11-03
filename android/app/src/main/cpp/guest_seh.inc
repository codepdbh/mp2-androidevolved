// Despacho de excepciones estructuradas sobre la pila invitada: lo que hace ntdll
// en Windows. Los manejadores son los de msvcr71 recompilados; el catch no necesita
// longjmp: la continuación corre encima de los marcos C del throw y, al volver,
// RECOMP_SEH_CHECK hace que los marcos muertos retornen en cadena (mismo modelo que
// desktop/pc_host/mp2_seh.c, validado en Windows).
constexpr uint32_t sehContextBytes=0x2cc,sehRecordBytes=80,sehChainEnd=UINT32_MAX;
constexpr uint32_t sehNonContinuable=1,sehUnwinding=2,sehExitUnwind=4;
constexpr uint32_t sehCxxException=0xe06d7363u;

std::string sehCxxTypeName(uint32_t record){
    if(read32(record)!=sehCxxException||read32(record+16)<3)return {};
    uint32_t info=read32(record+20+8);if(!info)return "(rethrow)";
    uint32_t array=read32(info+12),type=read32(array+4),descriptor=read32(type+4);
    return guestString(descriptor+8);
}
// Devuelve true si la pila quedó por encima de floor: ya corrió una continuación
// y este marco está muerto, con los registros de la continuación intactos.
bool sehCallHandler(uint32_t handler,uint32_t record,uint32_t frame,uint32_t context,uint32_t floor,uint32_t& result){
    recomp_func_t fn=recomp_lookup(handler);
    if(!fn)throw GuestStop("Manejador SEH sin recompilar "+hex(handler)+" (marco "+hex(frame)+")");
    uint32_t saved=g_esp,caller=g_cur_func;
    g_esp-=16;write32(g_esp,record);write32(g_esp+4,frame);write32(g_esp+8,context);write32(g_esp+12,0);
    g_esp-=4;write32(g_esp,RECOMP_RETADDR);
    fn();
    g_cur_func=caller;
    if(g_esp>floor)return true;
    result=g_eax;g_esp=saved;return false;
}
void raiseException(){
    uint32_t code=arg(0),flags=arg(1),count=arg(2),args=arg(3),floor=g_esp;
    if(code==0x406d1388u){ret(0,4);return;}  // SetThreadName de MSVC
    uint32_t savedEbx=g_ebx,savedEsi=g_esi,savedEdi=g_edi,savedEbp=g_ebp;
    g_esp-=(sehContextBytes+sehRecordBytes+15)&~15u;
    uint32_t context=g_esp,record=context+sehContextBytes;
    std::memset(ptr(context,sehContextBytes+sehRecordBytes),0,sehContextBytes+sehRecordBytes);
    write32(context,0x10007);  // CONTEXT_FULL
    write32(context+0x9c,g_edi);write32(context+0xa0,g_esi);write32(context+0xa4,g_ebx);write32(context+0xa8,g_edx);
    write32(context+0xac,g_ecx);write32(context+0xb0,g_eax);write32(context+0xb4,g_ebp);write32(context+0xb8,g_cur_func);
    write32(context+0xc4,floor+4+16);
    uint32_t parameters=std::min(count,15u);
    write32(record,code);write32(record+4,flags&sehNonContinuable);write32(record+12,g_cur_func);write32(record+16,parameters);
    if(args&&parameters)std::memcpy(ptr(record+20,parameters*4),ptr(args,parameters*4),parameters*4);
    auto type=sehCxxTypeName(record);
    static unsigned logged=0;
    if(++logged<=64)NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","SEH %08x %s desde %08x",code,type.c_str(),g_cur_func);
    for(uint32_t frame=read32(g_fs_base);frame!=sehChainEnd;frame=read32(frame)){
        uint32_t handler=read32(frame+4),result=0;
        if(sehCallHandler(handler,record,frame,context,floor,result))return;
        if(result==0){
            if(read32(record+4)&sehNonContinuable)throw GuestStop("Un manejador intentó continuar una excepción no continuable");
            g_ebx=savedEbx;g_esi=savedEsi;g_edi=savedEdi;g_ebp=savedEbp;g_esp=floor+4+16;return;
        }
        if(result!=1)throw GuestStop("Manejador "+hex(handler)+" devolvió "+std::to_string(result));
    }
    throw GuestStop("Excepción "+hex(code)+" sin manejador "+type+" desde "+hex(g_cur_func));
}
// RtlUnwind(marco, ip, registro, valor): llama a los manejadores con la bandera de
// unwind y retira sus marcos. ip es siempre la instrucción que sigue a la llamada
// en _global_unwind2 de msvcr71.
void rtlUnwind(){
    uint32_t target=arg(0),given=arg(2),value=arg(3),floor=g_esp;
    uint32_t savedEbx=g_ebx,savedEsi=g_esi,savedEdi=g_edi,savedEbp=g_ebp;
    g_esp-=(sehContextBytes+sehRecordBytes+15)&~15u;
    uint32_t context=g_esp,own=context+sehContextBytes;
    std::memset(ptr(context,sehContextBytes+sehRecordBytes),0,sehContextBytes+sehRecordBytes);
    write32(context,0x10007);
    uint32_t record=given?given:own;
    if(!given)write32(record,0xc0000027u);  // STATUS_UNWIND
    write32(record+4,read32(record+4)|sehUnwinding|(target?0:sehExitUnwind));
    for(uint32_t frame=read32(g_fs_base);frame!=sehChainEnd&&frame!=target;){
        uint32_t handler=read32(frame+4),result=0;
        if(sehCallHandler(handler,record,frame,context,floor,result))return;
        if(result!=1&&result!=3)throw GuestStop("Unwind: el manejador "+hex(handler)+" devolvió "+std::to_string(result));
        frame=read32(frame);write32(g_fs_base,frame);
    }
    if(target)write32(g_fs_base,target);
    write32(record+4,read32(record+4)&~(sehUnwinding|sehExitUnwind));
    g_ebx=savedEbx;g_esi=savedEsi;g_edi=savedEdi;g_ebp=savedEbp;g_eax=value;g_esp=floor+4+16;
}
