// Win32 que usan Max Payne 2 y MFC 7.1 y que los ports de NFS nunca necesitaron.
// Lista medida: scripts/pc_analysis/api_gap.py sobre work/pc/sandbox/api_usage.txt.
// Lo que no aparece aquí falla con "HLE pendiente" en vez de fingir que funcionó.

// ---- Cadenas ----
void k32lstrlenA(){ret(arg(0)?uint32_t(guestString(arg(0)).size()):0,1);}
void k32lstrcpyA(){auto text=guestString(arg(1));std::memcpy(ptr(arg(0),text.size()+1),text.c_str(),text.size()+1);ret(arg(0),2);}
void k32lstrcpynA(){
    uint32_t capacity=arg(2);if(!capacity){ret(arg(0),3);return;}
    auto text=guestString(arg(1)).substr(0,capacity-1);std::memcpy(ptr(arg(0),text.size()+1),text.c_str(),text.size()+1);ret(arg(0),3);
}
void k32lstrcatA(){auto text=guestString(arg(0))+guestString(arg(1));std::memcpy(ptr(arg(0),text.size()+1),text.c_str(),text.size()+1);ret(arg(0),2);}
void k32lstrcmpA(){int order=guestString(arg(0)).compare(guestString(arg(1)));ret(uint32_t(order<0?-1:order>0?1:0),2);}
void k32MulDiv(){
    int64_t value=int64_t(int32_t(arg(0)))*int32_t(arg(1));int32_t divisor=int32_t(arg(2));
    if(!divisor){ret(UINT32_MAX,3);return;}
    int64_t half=(divisor<0?-int64_t(divisor):divisor)/2;ret(uint32_t(int32_t((value+(value<0?-half:half))/divisor)),3);
}

// ---- Memoria movible: el handle es el puntero, nadie mueve bloques ----
uint32_t memoryAlloc(uint32_t flags,uint32_t bytes,uint32_t zeroFlag){return heaps->alloc(GuestHeaps::process,flags&zeroFlag?8:0,bytes);}
void k32GlobalAlloc2(){ret(memoryAlloc(arg(0),arg(1),0x40),2);}
void k32LocalAlloc(){ret(memoryAlloc(arg(0),arg(1),0x40),2);}
void k32LockHandle(){ret(arg(0),1);}
void k32UnlockHandle(){ret(1,1);}
void k32FreeHandle(){if(arg(0))heaps->free(GuestHeaps::process,arg(0));ret(0,1);}
void k32SizeHandle(){ret(arg(0)?heaps->size(GuestHeaps::process,arg(0)):0,1);}
void k32FlagsHandle(){ret(0,1);}
void k32ReAllocHandle(){ret(heaps->realloc(GuestHeaps::process,arg(2)&0x40?8:0,arg(0),arg(1)),3);}

// ---- Átomos globales ----
std::map<std::string,uint32_t> guestAtoms;
uint32_t atomFor(const std::string& name){
    auto key=lower(name);auto found=guestAtoms.find(key);if(found!=guestAtoms.end())return found->second;
    uint32_t atom=0xc000u+uint32_t(guestAtoms.size());guestAtoms[key]=atom;return atom;
}
void k32GlobalAddAtomA(){ret(atomFor(guestString(arg(0))),1);}
void k32GlobalFindAtomA(){auto found=guestAtoms.find(lower(guestString(arg(0))));ret(found==guestAtoms.end()?0:found->second,1);}
void k32GlobalDeleteAtom(){ret(0,1);}
void u32RegisterWindowMessageA(){ret(atomFor("message:"+guestString(arg(0))),1);}

// ---- Rutas y sistema ----
void copyToGuest(const std::string& text,uint32_t out,uint32_t capacity,uint32_t argc){
    if(capacity<=text.size()){ret(uint32_t(text.size()+1),argc);return;}
    std::memcpy(ptr(out,text.size()+1),text.c_str(),text.size()+1);ret(uint32_t(text.size()),argc);
}
void k32GetWindowsDirectoryA(){copyToGuest("C:\\WINDOWS",arg(0),arg(1),2);}
void k32GetTempPathA(){copyToGuest(std::string(guestRootPath)+"\\",arg(1),arg(0),2);}
void k32GetShortPathNameA(){copyToGuest(guestString(arg(0)),arg(1),arg(2),3);}
void k32SetCurrentDirectoryA(){
    auto text=guestString(arg(0));auto path=gamePath(text);std::error_code error;
    if(!std::filesystem::is_directory(path,error)){lastError=3;ret(0,1);return;}
    while(text.size()>3&&(text.back()=='\\'||text.back()=='/'))text.pop_back();
    if(text.find(':')==std::string::npos)text=guestWorkingDirectory+"\\"+text;
    guestWorkingDirectory=text;ret(1,1);
}
void k32ConvertDefaultLocale(){ret(arg(0)?arg(0):0x409,1);}
void k32SuspendThread(){  // Solo lo usan rutinas de diagnóstico de arranque: no detiene hilos invitados.
    NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSC","SuspendThread(%08x) desde %08x ignorado",arg(0),g_cur_func);ret(0,1);
}
void k32GetUILanguage(){ret(0x0c0a,0);}  // Español: MP2_Spanish.ras
void k32GetLocalTime(){
    struct ::timespec now{};::clock_gettime(CLOCK_REALTIME,&now);std::tm local{};time_t seconds=now.tv_sec;localtime_r(&seconds,&local);
    uint16_t out[8]={uint16_t(local.tm_year+1900),uint16_t(local.tm_mon+1),uint16_t(local.tm_wday),uint16_t(local.tm_mday),
                     uint16_t(local.tm_hour),uint16_t(local.tm_min),uint16_t(local.tm_sec),uint16_t(now.tv_nsec/1000000)};
    std::memcpy(ptr(arg(0),16),out,16);ret(0,1);
}
void k32FormatMessageA(){lastError=317;ret(0,7);}  // ERROR_MR_MID_NOT_FOUND: sin tablas de mensajes
void k32InitializeConditionVariable(){write32(arg(0),0);ret(0,1);}
void k32GetProfileIntA(){ret(arg(2),3);}  // win.ini vacío
void k32WritePrivateProfileStringA(){  // El plugin solo escribe sus preferencias: no se persisten.
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","WritePrivateProfileString [%s] ignorado",guestString(arg(0)).c_str());ret(1,4);
}
// ---- Shell ----
void shellGetSpecialFolderPathA(){
    uint32_t out=arg(1),folder=arg(2)&0xff,create=arg(3);std::string suffix;
    switch(folder){
    case 5:suffix="Documents";break;
    case 0x1a:suffix="AppData\\Roaming";break;
    case 0x1c:suffix="AppData\\Local";break;
    default:*static_cast<char*>(ptr(out,1))=0;ret(0,4);return;
    }
    auto logical=std::string(guestRootPath)+"\\"+suffix;std::error_code error;
    if(create)std::filesystem::create_directories(gamePath(logical),error);
    std::memcpy(ptr(out,logical.size()+1),logical.c_str(),logical.size()+1);ret(1,4);
}
void shlwPathFindFileNameA(){
    auto text=guestString(arg(0));auto at=text.find_last_of("\\/:");ret(arg(0)+(at==std::string::npos?0:uint32_t(at+1)),1);
}
void shlwPathFindExtensionA(){
    auto text=guestString(arg(0));auto slash=text.find_last_of("\\/:");auto dot=text.find_last_of('.');
    ret(arg(0)+uint32_t(dot==std::string::npos||(slash!=std::string::npos&&dot<slash)?text.size():dot),1);
}
void shlwPathRemoveExtensionA(){
    auto text=guestString(arg(0));auto slash=text.find_last_of("\\/:");auto dot=text.find_last_of('.');
    if(dot!=std::string::npos&&(slash==std::string::npos||dot>slash))*static_cast<char*>(ptr(arg(0)+uint32_t(dot),1))=0;
    ret(0,1);
}
// ---- COM y OLE: dxdiag no existe en Android; quien lo pide recibe el error real ----
void oleCoInitialize(){ret(0,1);}
void oleCoCreateInstance(){if(arg(4))write32(arg(4),0);ret(0x80040154u,5);}  // REGDB_E_CLASSNOTREG
void oleVariantInit(){std::memset(ptr(arg(0),16),0,16);ret(0,1);}
void oleVariantClear(){std::memset(ptr(arg(0),2),0,2);ret(0,1);}
// ---- Direct3D 8 (exports de d3d8.dll) ----
void d3dDebugSetMute(){ret(0,0);}
// ValidatePixelShader(código, caps, detalles, errores) y ValidateVertexShader(código, decl, caps, detalles, errores):
// el frontend D3D8 valida al crear el shader, así que aquí se acepta y se dice "sin errores".
void d3dValidatePixelShader(){if(arg(3))write32(arg(3),0);ret(0,4);}
void d3dValidateVertexShader(){if(arg(4))write32(arg(4),0);ret(0,5);}
// ---- DirectInput / DirectSound ----
void directInputCreateA(){  // (hinst, versión, salida, outer)
    uint32_t output=arg(2);if(!output){ret(0x80004003,4);return;}write32(output,0);
    if(arg(3)){ret(0x80040110,4);return;}
    write32(output,createInputObject(1));ret(0,4);
}
// Como en Windows: el controlador primario sin GUID y el dispositivo real con GUID. sndmfc
// descarta las entradas sin GUID; sin la segunda elige su "Null Audio Device" y todo calla.
void directSoundEnumerateA(){
    static uint32_t deviceGuid=0;
    if(!deviceGuid){deviceGuid=allocate(16);static const uint8_t guid[16]={0x4d,0x50,0x32,0x41,0x55,0x44,0x49,0x4f,0xaa,0xaa,0x41,0x41,0x55,0x44,0x49,0x4f};std::memcpy(ptr(deviceGuid,16),guid,16);}
    uint32_t primary=ansiString("Primary Sound Driver"),description=ansiString("Android AAudio"),module=ansiString("aaudio.dll"),empty=ansiString("");
    uint32_t callback=arg(0),context=arg(1);
    if(callGuest(callback,{0,primary,empty,context}))callGuest(callback,{deviceGuid,description,module,context});
    for(uint32_t text:{primary,description,module,empty})heaps->free(GuestHeaps::process,text);
    ret(0,2);
}
// ---- GDI: solo consultas de capacidades, sin dibujo ----
constexpr uint32_t guestDeviceContext=0xf0071000u;
void u32GetDC(){ret(guestDeviceContext,1);}
void u32ReleaseDC(){ret(1,2);}
void gdiGetDeviceCaps(){
    uint32_t value=0;
    switch(arg(1)){
    case 8:value=displayModeWidth();break;case 10:value=displayModeHeight();break;
    case 12:value=32;break;case 14:value=1;break;case 88:case 90:value=96;break;case 116:value=60;break;
    }
    ret(value,2);
}
// ---- Ventanas: lo que MFC necesita ----
void u32RegisterClassA(){
    uint32_t value=arg(0);if(!value){lastError=87;ret(0,1);return;}
    auto name=read32(value+36)>0xffff?guestString(read32(value+36)):std::string("#")+std::to_string(read32(value+36));
    if(windowClasses.count(name)){lastError=1410;ret(0,1);return;}
    GuestWindowClass record{nextClassAtom++,read32(value+4),read32(value+16),read32(value),read32(value+12),name};
    uint32_t atom=record.atom;windowClasses.emplace(name,std::move(record));ret(atom,1);
}
const GuestWindowClass* windowClassBy(uint32_t nameOrAtom){
    if(nameOrAtom<=0xffff){for(auto& record:windowClasses)if(record.second.atom==nameOrAtom)return &record.second;return nullptr;}
    auto found=windowClasses.find(guestString(nameOrAtom));return found==windowClasses.end()?nullptr:&found->second;
}
void fillClassInfo(uint32_t out,const GuestWindowClass& cls,uint32_t name){
    write32(out,cls.style);write32(out+4,cls.procedure);write32(out+8,0);write32(out+12,cls.extra);write32(out+16,cls.instance);
    write32(out+20,0);write32(out+24,0);write32(out+28,0);write32(out+32,0);write32(out+36,name);
}
void u32GetClassInfoA(){
    auto* cls=windowClassBy(arg(1));if(!cls){lastError=1411;ret(0,3);return;}
    fillClassInfo(arg(2),*cls,arg(1));ret(cls->atom,3);
}
void u32GetClassInfoExA(){
    auto* cls=windowClassBy(arg(1));if(!cls){lastError=1411;ret(0,3);return;}
    uint32_t out=arg(2);write32(out,48);write32(out+44,0);fillClassInfo(out+4,*cls,arg(1));ret(cls->atom,3);
}
void u32GetClassLongA(){
    auto window=windows.find(arg(0));if(window==windows.end()){lastError=1400;ret(0,2);return;}
    const GuestWindowClass* cls=nullptr;for(auto& record:windowClasses)if(record.second.name==window->second.className)cls=&record.second;
    if(!cls){ret(0,2);return;}
    switch(int32_t(arg(1))){
    case -32:ret(cls->atom,2);return;case -26:ret(cls->style,2);return;case -24:ret(cls->procedure,2);return;
    case -20:ret(cls->extra,2);return;case -16:ret(cls->instance,2);return;default:ret(0,2);return;
    }
}
void u32GetClassNameA(){
    auto window=windows.find(arg(0));if(window==windows.end()){lastError=1400;ret(0,3);return;}
    auto name=window->second.className.substr(0,arg(2)?arg(2)-1:0);
    if(arg(2))std::memcpy(ptr(arg(1),name.size()+1),name.c_str(),name.size()+1);ret(uint32_t(name.size()),3);
}
void u32CallWindowProcA(){ret(callGuest(arg(0),{arg(1),arg(2),arg(3),arg(4)}),5);}
void u32IsWindow(){ret(windows.count(arg(0))?1:0,1);}
void u32IsWindowVisible(){auto window=windows.find(arg(0));ret(window!=windows.end()&&window->second.visible?1:0,1);}
void u32IsWindowEnabled(){ret(windows.count(arg(0))?1:0,1);}
void u32GetParent(){auto window=windows.find(arg(0));if(window==windows.end()){lastError=1400;ret(0,1);return;}ret(window->second.parent,1);}
void u32GetWindow(){  // GW_OWNER (4) es el padre; Z-order y GW_CHILD: el juego solo tiene ventanas de primer nivel.
    auto window=windows.find(arg(0));if(window==windows.end()){lastError=1400;ret(0,2);return;}
    ret(arg(1)==4?window->second.parent:0,2);
}
void u32Zero1(){ret(0,1);}
void u32Zero2(){ret(0,2);}
void u32One1(){ret(1,1);}
void u32One3(){ret(1,3);}
void u32GetWindowThreadProcessId(){
    if(arg(1))write32(arg(1),uint32_t(getpid()));
    auto window=windows.find(arg(0));ret(window==windows.end()?0:window->second.thread,2);
}
void u32FindWindowA(){ret(0,2);}  // Ninguna otra ventana existe en el proceso.
void u32GetTopWindow(){ret(0,1);}
void u32DestroyWindow(){
    uint32_t handle=arg(0);if(!windows.count(handle)){lastError=1400;ret(0,1);return;}
    if(activeWindow==handle){activeWindow=0;windowMessage(handle,0x1c,0,0);}
    windowMessage(handle,2,0,0);windowMessage(handle,0x82,0,0);windows.erase(handle);ret(1,1);
}
void u32SetPropA(){
    auto window=windows.find(arg(0));if(window==windows.end()){lastError=1400;ret(0,3);return;}
    auto key=arg(1)>0xffff?lower(guestString(arg(1))):std::to_string(arg(1));window->second.properties[key]=arg(2);ret(1,3);
}
void u32GetPropA(){
    auto window=windows.find(arg(0));if(window==windows.end()){ret(0,2);return;}
    auto key=arg(1)>0xffff?lower(guestString(arg(1))):std::to_string(arg(1));
    auto value=window->second.properties.find(key);ret(value==window->second.properties.end()?0:value->second,2);
}
void u32RemovePropA(){
    auto window=windows.find(arg(0));if(window==windows.end()){ret(0,2);return;}
    auto key=arg(1)>0xffff?lower(guestString(arg(1))):std::to_string(arg(1));
    auto value=window->second.properties.find(key);if(value==window->second.properties.end()){ret(0,2);return;}
    uint32_t data=value->second;window->second.properties.erase(value);ret(data,2);
}
void u32GetSysColor(){ret(0x00c0c0c0u,1);}
void u32GetSysColorBrush(){ret(0xf00c0000u+(arg(0)&0xff),1);}
void u32LoadAcceleratorsA(){ret(0xf00d0001u,2);}
void u32TranslateAcceleratorA(){ret(0,3);}
// ---- Ganchos: el CBT es la pieza central de CWnd::Create en MFC ----
struct GuestHook {uint32_t handle,type,procedure;};
std::vector<GuestHook> guestHooks;uint32_t nextGuestHook=0xf0090001u;
void u32SetWindowsHookExA(){
    int32_t type=int32_t(arg(0));uint32_t handle=nextGuestHook++;
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","SetWindowsHookExA tipo=%d proc=%08x hilo=%u",type,arg(1),arg(3));
    if(type!=13&&type!=14)guestHooks.push_back({handle,uint32_t(type),arg(1)});  // Los _LL globales no se instalan.
    ret(handle,4);
}
void u32UnhookWindowsHookEx(){
    auto found=std::find_if(guestHooks.begin(),guestHooks.end(),[](const GuestHook& hook){return hook.handle==arg(0);});
    if(found!=guestHooks.end())guestHooks.erase(found);ret(1,1);
}
void u32CallNextHookEx(){  // (hook, code, wparam, lparam): la cadena sigue por el mismo tipo.
    auto current=std::find_if(guestHooks.begin(),guestHooks.end(),[](const GuestHook& hook){return hook.handle==arg(0);});
    if(current!=guestHooks.end()){
        uint32_t type=current->type;
        for(auto next=current+1;next!=guestHooks.end();++next)if(next->type==type){ret(callGuest(next->procedure,{arg(1),arg(2),arg(3)}),4);return;}
    }
    ret(0,4);
}
void runWindowHooks(uint32_t type,uint32_t code,uint32_t wparam,uint32_t lparam){
    auto copy=guestHooks;  // El gancho CBT de MFC se desinstala a sí mismo dentro de la llamada.
    for(auto& hook:copy)if(hook.type==type){callGuest(hook.procedure,{code,wparam,lparam});return;}
}
void callCreateHooks(uint32_t handle,uint32_t createStruct){
    uint32_t cbt=allocate(8);write32(cbt,createStruct);write32(cbt+4,0);
    runWindowHooks(5,3,handle,cbt);  // WH_CBT, HCBT_CREATEWND
    heaps->free(GuestHeaps::process,cbt);
}
// ---- Teclado para el gancho WH_KEYBOARD y el texto ----
void u32GetKeyboardState(){
    std::memset(ptr(arg(0),256),0,256);
    std::lock_guard<std::mutex> lock(inputMutex);
    for(unsigned vk=1;vk<256;++vk){unsigned scan=mapVirtualKey(vk,0);if(scan&&scan<256&&inputKeys[scan])*static_cast<uint8_t*>(ptr(arg(0)+vk,1))=0x80;}
    ret(1,1);
}
void u32ToAscii(){  // (vk, scan, estado, salida, flags)
    uint32_t vk=arg(0);char c=0;
    if(vk>='A'&&vk<='Z')c=char(vk+32);else if((vk>='0'&&vk<='9')||vk==32||vk==13||vk==8||vk==9)c=char(vk);
    if(!c){ret(0,5);return;}
    *static_cast<uint8_t*>(ptr(arg(3),2))=uint8_t(c);ret(1,5);
}
void u32GetAsyncKeyState(){
    unsigned scan=mapVirtualKey(arg(0),0);bool down=false;
    {std::lock_guard<std::mutex> lock(inputMutex);down=scan&&scan<256&&inputKeys[scan];}
    ret(down?0x8000u:0u,1);
}
// ---- Diálogos: el de inicio se evita con los ajustes válidos del registro virtual ----
void u32CreateDialogIndirectParamA(){
    throw GuestStop("El juego pidió el diálogo de ajustes: los valores de Video Settings no le sirven (desde "+hex(g_cur_func)+")");
}
void u32SystemParametersInfoA(){  // Solo consultas; nada cambia ajustes del sistema.
    uint32_t action=arg(0),out=arg(2);
    if(action==0x30&&out){int32_t rect[4]={0,0,int32_t(displayModeWidth()),int32_t(displayModeHeight())};std::memcpy(ptr(out,16),rect,16);}
    ret(1,4);
}
void u32ChangeDisplaySettingsA(){ret(0,2);}  // DISP_CHANGE_SUCCESSFUL: el panel no cambia de modo.

// ---- Lote de MFC: rectángulos, texto de ventana y consultas sin efecto en el juego ----
struct GuestRect{int32_t l,t,r,b;};
GuestRect readRect(uint32_t at){GuestRect v;std::memcpy(&v,ptr(at,16),16);return v;}
void writeRect(uint32_t at,const GuestRect& v){std::memcpy(ptr(at,16),&v,16);}
void u32SetRect(){writeRect(arg(0),{int32_t(arg(1)),int32_t(arg(2)),int32_t(arg(3)),int32_t(arg(4))});ret(1,5);}
void u32SetRectEmpty(){writeRect(arg(0),{0,0,0,0});ret(1,1);}
void u32CopyRect(){writeRect(arg(0),readRect(arg(1)));ret(1,2);}
void u32IsRectEmpty(){auto v=readRect(arg(0));ret(v.l>=v.r||v.t>=v.b,1);}
void u32OffsetRect(){auto v=readRect(arg(0));int32_t x=arg(1),y=arg(2);writeRect(arg(0),{v.l+x,v.t+y,v.r+x,v.b+y});ret(1,3);}
void u32InflateRect(){auto v=readRect(arg(0));int32_t x=arg(1),y=arg(2);writeRect(arg(0),{v.l-x,v.t-y,v.r+x,v.b+y});ret(1,3);}
void u32EqualRect(){auto a=readRect(arg(0)),b=readRect(arg(1));ret(a.l==b.l&&a.t==b.t&&a.r==b.r&&a.b==b.b,2);}
void u32PtInRect(){auto v=readRect(arg(0));int32_t x=arg(1),y=arg(2);ret(x>=v.l&&x<v.r&&y>=v.t&&y<v.b,3);}
void u32IntersectRect(){auto a=readRect(arg(1)),b=readRect(arg(2));GuestRect v{std::max(a.l,b.l),std::max(a.t,b.t),std::min(a.r,b.r),std::min(a.b,b.b)};
    bool some=v.l<v.r&&v.t<v.b;writeRect(arg(0),some?v:GuestRect{0,0,0,0});ret(some,3);}
void u32UnionRect(){auto a=readRect(arg(1)),b=readRect(arg(2));writeRect(arg(0),{std::min(a.l,b.l),std::min(a.t,b.t),std::max(a.r,b.r),std::max(a.b,b.b)});ret(1,3);}
void u32AdjustWindowRectEx(){ret(1,4);}  // Ventana sin marco: cliente y ventana miden lo mismo.
void u32MapWindowPoints(){ret(0,4);}     // Todas las ventanas están en el origen.
void u32CharNextA(){uint32_t at=arg(0);ret(*static_cast<char*>(ptr(at,1))?at+1:at,1);}
void u32CharLowerA(){
    uint32_t value=arg(0);if(!(value>>16)){ret(uint32_t(std::tolower(int(value&0xff))),1);return;}
    for(uint32_t at=value;;++at){auto* c=static_cast<uint8_t*>(ptr(at,1));if(!*c)break;*c=uint8_t(std::tolower(*c));}ret(value,1);
}
std::map<uint32_t,std::string> guestWindowTexts;
void u32SetWindowTextA(){guestWindowTexts[arg(0)]=arg(1)?guestString(arg(1)):std::string();ret(1,2);}
void u32GetWindowTextLengthA(){ret(uint32_t(guestWindowTexts[arg(0)].size()),1);}
void u32GetWindowTextA(){
    auto text=guestWindowTexts[arg(0)];uint32_t capacity=arg(2);if(!capacity){ret(0,3);return;}
    text=text.substr(0,capacity-1);std::memcpy(ptr(arg(1),text.size()+1),text.c_str(),text.size()+1);ret(uint32_t(text.size()),3);
}
void u32GetMessageTime(){ret(messageTime(),0);}
void u32GetMessagePos(){auto point=pointerInWindow();ret(uint32_t(point.first&0xffff)|uint32_t(point.second&0xffff)<<16,0);}
void u32Zero0(){ret(0,0);}
void u32Zero3(){ret(0,3);}
void u32Zero4(){ret(0,4);}
void u32One2(){ret(1,2);}
void u32One4(){ret(1,4);}
void u32Self1(){ret(arg(0),1);}
void u32WaitMessage(){checkGuestProgress();yieldGuestMachine(1);ret(1,0);}
// ---- GDI sin dibujo: handles válidos para quien solo los guarda ----
uint32_t nextGdiHandle=0xf00f0000u;
void gdiNew1(){ret(nextGdiHandle++,1);}
void gdiNew3(){ret(nextGdiHandle++,3);}
void gdiCreateFontIndirectA(){ret(nextGdiHandle++,1);}
void gdiDelete(){ret(1,1);}
void gdiSelectObject(){ret(arg(1),2);}
void gdiSaveDC(){ret(1,1);}
void gdiRestoreDC(){ret(1,2);}
// ---- Kernel: cadenas anchas y locale ----
uint32_t wideLength(uint32_t at){uint32_t n=0;for(;;++n){uint16_t c;std::memcpy(&c,ptr(at+n*2,2),2);if(!c)return n;}}
void k32lstrlenW(){ret(arg(0)?wideLength(arg(0)):0,1);}
void k32lstrcpyW(){uint32_t n=wideLength(arg(1));std::memmove(ptr(arg(0),(n+1)*2),ptr(arg(1),(n+1)*2),(n+1)*2);ret(arg(0),2);}
void k32lstrcmpW(){auto a=wideString(arg(0)),b=wideString(arg(1));ret(a<b?UINT32_MAX:a==b?0:1,2);}
void k32ReturnOne1(){ret(1,1);}
void k32ReturnOne2(){ret(1,2);}
void k32GetConsoleCP(){ret(437,0);}
void k32IsValidLocale(){ret(1,2);}
void k32GetVolumeInformationA(){
    if(arg(1)&&arg(2)){static const char label[]="MP2";std::memcpy(ptr(arg(1),sizeof label),label,sizeof label);}
    if(arg(3))write32(arg(3),0x4d503200u);if(arg(4))write32(arg(4),255);if(arg(5))write32(arg(5),0x3);
    if(arg(6)&&arg(7)){static const char fs[]="NTFS";std::memcpy(ptr(arg(6),sizeof fs),fs,sizeof fs);}
    ret(1,8);
}
void k32GetStringTypeA(){  // (lcid, tipo, cadena, n, salida): CT_CTYPE1 sobre ASCII/1252
    uint32_t text=arg(2),out=arg(4);int32_t count=int32_t(arg(3));if(count<0)count=int32_t(guestString(text).size())+1;
    for(int32_t i=0;i<count;++i){int c=*static_cast<uint8_t*>(ptr(text+i,1));uint16_t v=0;
        if(c<128){if(std::isupper(c))v|=1;if(std::islower(c))v|=2;if(std::isdigit(c))v|=4;if(std::isspace(c))v|=8;if(std::ispunct(c))v|=0x10;if(std::iscntrl(c))v|=0x20;if(c==' ')v|=0x40;if(std::isxdigit(c))v|=0x80;if(std::isalpha(c))v|=0x100;}
        std::memcpy(ptr(out+i*2,2),&v,2);}
    ret(1,5);
}
void k32LCMapStringA(){  // (lcid, flags, origen, n, destino, capacidad): mayúsculas/minúsculas ASCII
    uint32_t flags=arg(1),source=arg(2),out=arg(4),capacity=arg(5);int32_t count=int32_t(arg(3));
    if(count<0)count=int32_t(guestString(source).size())+1;
    if(!capacity){ret(count,6);return;}
    if(capacity<uint32_t(count)){lastError=122;ret(0,6);return;}
    for(int32_t i=0;i<count;++i){int c=*static_cast<uint8_t*>(ptr(source+i,1));if(flags&0x100)c=std::tolower(c);else if(flags&0x200)c=std::toupper(c);*static_cast<uint8_t*>(ptr(out+i,1))=uint8_t(c);}
    ret(count,6);
}
const std::map<std::string,recomp_func_t> mp2KernelFunctions={
    {"lstrlenA",k32lstrlenA},{"lstrcpyA",k32lstrcpyA},{"lstrcpynA",k32lstrcpynA},{"lstrcatA",k32lstrcatA},{"lstrcmpA",k32lstrcmpA},{"MulDiv",k32MulDiv},
    {"GlobalAlloc",k32GlobalAlloc2},{"GlobalLock",k32LockHandle},{"GlobalUnlock",k32UnlockHandle},{"GlobalFree",k32FreeHandle},{"GlobalSize",k32SizeHandle},
    {"GlobalFlags",k32FlagsHandle},{"GlobalHandle",k32LockHandle},{"GlobalReAlloc",k32ReAllocHandle},
    {"LocalAlloc",k32LocalAlloc},{"LocalLock",k32LockHandle},{"LocalUnlock",k32UnlockHandle},{"LocalFree",k32FreeHandle},{"LocalReAlloc",k32ReAllocHandle},
    {"GlobalAddAtomA",k32GlobalAddAtomA},{"GlobalFindAtomA",k32GlobalFindAtomA},{"GlobalDeleteAtom",k32GlobalDeleteAtom},
    {"GetWindowsDirectoryA",k32GetWindowsDirectoryA},{"GetSystemDirectoryA",k32GetSystemDirectoryA},{"GetComputerNameA",k32GetComputerNameA},
    {"GetTempPathA",k32GetTempPathA},{"GetShortPathNameA",k32GetShortPathNameA},{"SetCurrentDirectoryA",k32SetCurrentDirectoryA},
    {"ConvertDefaultLocale",k32ConvertDefaultLocale},{"SuspendThread",k32SuspendThread},{"GetLocalTime",k32GetLocalTime},{"FormatMessageA",k32FormatMessageA},
    {"GetSystemDefaultUILanguage",k32GetUILanguage},{"GetUserDefaultUILanguage",k32GetUILanguage},{"InitializeConditionVariable",k32InitializeConditionVariable},
    {"GetProfileIntA",k32GetProfileIntA},{"WritePrivateProfileStringA",k32WritePrivateProfileStringA},{"RtlUnwind",rtlUnwind},
    {"lstrlenW",k32lstrlenW},{"lstrcpyW",k32lstrcpyW},{"lstrcmpW",k32lstrcmpW},{"lstrcmpiW",k32lstrcmpW},
    {"GetConsoleCP",k32GetConsoleCP},{"GetConsoleOutputCP",k32GetConsoleCP},{"IsValidLocale",k32IsValidLocale},{"SetStdHandle",k32ReturnOne2},
    {"SetConsoleCtrlHandler",k32ReturnOne2},{"GetVolumeInformationA",k32GetVolumeInformationA},{"GetStringTypeA",k32GetStringTypeA},{"LCMapStringA",k32LCMapStringA},
    {"HeapCompact",k32ReturnOne2},{"GlobalGetAtomNameA",k32ReturnOne1},
};
const std::map<std::string,recomp_func_t> mp2UserFunctions={
    {"RegisterClassA",u32RegisterClassA},{"GetClassInfoA",u32GetClassInfoA},{"GetClassInfoExA",u32GetClassInfoExA},{"GetClassLongA",u32GetClassLongA},{"GetClassNameA",u32GetClassNameA},
    {"CallWindowProcA",u32CallWindowProcA},{"IsWindow",u32IsWindow},{"IsWindowVisible",u32IsWindowVisible},{"IsWindowEnabled",u32IsWindowEnabled},
    {"GetParent",u32GetParent},{"GetWindow",u32GetWindow},{"GetWindowThreadProcessId",u32GetWindowThreadProcessId},{"FindWindowA",u32FindWindowA},{"GetTopWindow",u32GetTopWindow},
    {"DestroyWindow",u32DestroyWindow},{"SetPropA",u32SetPropA},{"GetPropA",u32GetPropA},{"RemovePropA",u32RemovePropA},
    {"GetSysColor",u32GetSysColor},{"GetSysColorBrush",u32GetSysColorBrush},{"LoadAcceleratorsA",u32LoadAcceleratorsA},{"TranslateAcceleratorA",u32TranslateAcceleratorA},
    {"RegisterWindowMessageA",u32RegisterWindowMessageA},{"InvalidateRect",u32One3},{"ValidateRect",u32Zero2},{"GetDC",u32GetDC},{"GetWindowDC",u32GetDC},{"ReleaseDC",u32ReleaseDC},
    {"SetWindowsHookExA",u32SetWindowsHookExA},{"UnhookWindowsHookEx",u32UnhookWindowsHookEx},{"CallNextHookEx",u32CallNextHookEx},
    {"GetKeyboardState",u32GetKeyboardState},{"ToAscii",u32ToAscii},{"GetAsyncKeyState",u32GetAsyncKeyState},
    {"CreateDialogIndirectParamA",u32CreateDialogIndirectParamA},{"SystemParametersInfoA",u32SystemParametersInfoA},{"ChangeDisplaySettingsA",u32ChangeDisplaySettingsA},
    {"ClipCursor",u32One1},{"EnableWindow",u32Zero2},{"BringWindowToTop",u32One1},
    {"RegisterClipboardFormatA",u32RegisterWindowMessageA},{"SetRect",u32SetRect},{"SetRectEmpty",u32SetRectEmpty},{"CopyRect",u32CopyRect},{"IsRectEmpty",u32IsRectEmpty},
    {"OffsetRect",u32OffsetRect},{"InflateRect",u32InflateRect},{"EqualRect",u32EqualRect},{"PtInRect",u32PtInRect},{"IntersectRect",u32IntersectRect},{"UnionRect",u32UnionRect},
    {"AdjustWindowRectEx",u32AdjustWindowRectEx},{"MapWindowPoints",u32MapWindowPoints},{"CharNextA",u32CharNextA},{"CharLowerA",u32CharLowerA},
    {"SetWindowTextA",u32SetWindowTextA},{"GetWindowTextA",u32GetWindowTextA},{"GetWindowTextLengthA",u32GetWindowTextLengthA},{"GetMessageTime",u32GetMessageTime},{"GetMessagePos",u32GetMessagePos},
    {"IsZoomed",u32Zero1},{"GetMenu",u32Zero1},{"GetSystemMenu",u32Zero2},{"GetSubMenu",u32Zero2},{"IsChild",u32Zero2},{"GetLastActivePopup",u32Self1},{"GetDlgCtrlID",u32Zero1},{"InSendMessage",u32Zero0},
    {"SetTimer",u32One4},{"KillTimer",u32One2},{"WaitMessage",u32WaitMessage},{"MessageBeep",u32One1},{"DestroyCursor",u32One1},{"DestroyIcon",u32One1},{"DestroyMenu",u32One1},
    {"RedrawWindow",u32One4},{"UnregisterClassA",u32One2},{"WindowFromPoint",u32Zero2},{"LockWindowUpdate",u32One1},{"DrawMenuBar",u32One1},
    {"GetMonitorInfoA",userGetMonitorInfoA},{"MonitorFromWindow",userMonitorFromWindow},{"ClientToScreen",userIdentityPoint},{"ScreenToClient",userIdentityPoint},
};
recomp_func_t resolveMp2Function(const std::string& dll,const std::string& name){
    auto find=[&](const std::map<std::string,recomp_func_t>& table)->recomp_func_t{auto found=table.find(name);return found==table.end()?nullptr:found->second;};
    if(dll=="advapi32.dll")return find(registryFunctions);
    if(dll=="kernel32.dll")return find(mp2KernelFunctions);
    if(dll=="user32.dll")return find(mp2UserFunctions);
    if(dll=="gdi32.dll"){
        if(name=="GetDeviceCaps")return gdiGetDeviceCaps;
        if(name=="CreateSolidBrush"||name=="CreateCompatibleDC"||name=="CreateRectRgnIndirect")return gdiNew1;
        if(name=="CreateFontIndirectA")return gdiCreateFontIndirectA;
        if(name=="DeleteDC"||name=="DeleteObject")return gdiDelete;
        if(name=="SelectObject")return gdiSelectObject;
        if(name=="SaveDC")return gdiSaveDC;
        if(name=="RestoreDC")return gdiRestoreDC;
    }
    if(dll=="ole32.dll"){if(name=="CoInitialize")return oleCoInitialize;if(name=="CoCreateInstance")return oleCoCreateInstance;}
    if(dll=="oleaut32.dll"){if(name=="#8")return oleVariantInit;if(name=="#9")return oleVariantClear;}
    if(dll=="shlwapi.dll"){if(name=="PathFindFileNameA")return shlwPathFindFileNameA;if(name=="PathFindExtensionA")return shlwPathFindExtensionA;if(name=="PathRemoveExtensionA")return shlwPathRemoveExtensionA;}
    if(dll=="shell32.dll"&&name=="SHGetSpecialFolderPathA")return shellGetSpecialFolderPathA;
    if(dll=="dinput.dll"&&name=="DirectInputCreateA")return directInputCreateA;
    if(dll=="dsound.dll"){if(name=="#1"||name=="DirectSoundCreate"||name=="#11"||name=="DirectSoundCreate8")return directSoundCreate;if(name=="#2"||name=="DirectSoundEnumerateA")return directSoundEnumerateA;}
    if(dll=="eax.dll"){if(name=="#5"||name=="EAXDirectSoundCreate"||name=="#6"||name=="EAXDirectSoundCreate8")return directSoundCreate;}
    if(dll=="d3d8.dll"){
        if(name=="Direct3DCreate8")return direct3DCreate;
        if(name=="DebugSetMute")return d3dDebugSetMute;
        if(name=="ValidatePixelShader")return d3dValidatePixelShader;
        if(name=="ValidateVertexShader")return d3dValidateVertexShader;
    }
    return nullptr;
}
