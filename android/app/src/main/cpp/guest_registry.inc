// Registro virtual del juego: mismo formato que desktop/pc_host/mp2_registry.c
// (clave|nombre|tipo|bytes en hexadecimal), guardado en la carpeta del juego.
// Los ajustes de vídeo los escribe el runtime antes de arrancar: son los que hacen
// que -nodialog salte el diálogo de inicio, y Display Width/Height son los que usa
// el WidescreenFix para calcular la relación de aspecto.
struct RegistryValue {uint32_t type;std::vector<uint8_t> bytes;};
std::map<std::string,std::map<std::string,RegistryValue>> registryKeys;   // ruta en minúsculas -> valores
std::map<std::string,std::string> registryDisplayNames;                   // ruta -> ruta original
std::map<uint32_t,std::string> registryHandles;
uint32_t nextRegistryHandle=0xf0050000;
constexpr const char* registrySettingsKey="hkcu\\software\\remedy entertainment\\max payne 2\\video settings";
std::string registryFile(){return hostGameRoot+"/android_registry.txt";}
std::string registryRoot(uint32_t handle){
    switch(handle){
    case 0x80000000u:return "hkcr";case 0x80000001u:return "hkcu";case 0x80000002u:return "hklm";
    case 0x80000003u:return "hku";case 0x80000005u:return "hkcc";
    }
    auto found=registryHandles.find(handle);return found==registryHandles.end()?std::string():found->second;
}
void registryAddKey(const std::string& path){
    for(size_t at=path.find('\\');;at=path.find('\\',at+1)){
        registryKeys[path.substr(0,at)];
        if(at==std::string::npos)break;
    }
}
void saveRegistry(){
    std::string target=registryFile(),temporary=target+".tmp";
    {std::ofstream file(temporary,std::ios::trunc);if(!file)return;
     static const char digits[]="0123456789abcdef";
     for(auto& [key,values]:registryKeys)for(auto& [name,value]:values){
         file<<key<<'|'<<name<<'|'<<value.type<<'|';
         for(uint8_t byte:value.bytes)file<<digits[byte>>4]<<digits[byte&15];
         file<<'\n';
     }
     if(!file)return;}
    std::rename(temporary.c_str(),target.c_str());
}
void loadRegistry(){
    std::ifstream file(registryFile());std::string line;unsigned restored=0;
    while(std::getline(file,line)){
        auto first=line.find('|'),second=line.find('|',first+1),third=line.find('|',second+1);
        if(third==std::string::npos)continue;
        RegistryValue value{uint32_t(std::strtoul(line.c_str()+second+1,nullptr,10)),{}};
        for(size_t at=third+1;at+1<line.size();at+=2)value.bytes.push_back(uint8_t(std::strtoul(line.substr(at,2).c_str(),nullptr,16)));
        auto key=lower(line.substr(0,first));registryAddKey(key);
        registryKeys[key][lower(line.substr(first+1,second-first-1))]=std::move(value);++restored;
    }
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Registro virtual: %u valores restaurados",restored);
}
void registrySet(const std::string& key,const std::string& name,uint32_t type,const void* data,size_t size){
    registryAddKey(key);auto bytes=static_cast<const uint8_t*>(data);registryKeys[key][name]=RegistryValue{type,{bytes,bytes+size}};
}
void registrySetNumber(const char* key,const char* name,uint32_t value){registrySet(key,name,4,&value,4);}
// Nombre fijo del adaptador: d3d8_bridge.cpp informa el mismo en GetAdapterIdentifier.
void seedRegistry(uint32_t width,uint32_t height){
    registryAddKey("hkcu\\software");registryAddKey("hklm\\software");
    loadRegistry();
    static const char driver[]="Android Vulkan";
    registrySet(registrySettingsKey,"driver",1,driver,sizeof driver);
    registrySetNumber(registrySettingsKey,"display width",width);
    registrySetNumber(registrySettingsKey,"display height",height);
    registrySetNumber(registrySettingsKey,"display bits per pixel",32);
    registrySetNumber(registrySettingsKey,"cpu driver",1);
    registrySetNumber(registrySettingsKey,"hw acceleration level",4);
    saveRegistry();
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Registro virtual: vídeo %ux%u, adaptador %s",width,height,driver);
}
uint32_t registryOpen(uint32_t parent,uint32_t subkey,uint32_t output,bool create){
    if(!output)return 87;
    auto base=registryRoot(parent);if(base.empty()){write32(output,0);return 6;}
    auto sub=subkey?lower(guestString(subkey)):std::string();
    while(!sub.empty()&&sub.back()=='\\')sub.pop_back();
    auto path=sub.empty()?base:base+"\\"+sub;
    if(!registryKeys.count(path)){
        if(!create){write32(output,0);return 2;}
        registryAddKey(path);saveRegistry();
    }
    uint32_t handle=nextRegistryHandle++;registryHandles[handle]=path;write32(output,handle);
    return 0;
}
void regOpenKeyA(){ret(registryOpen(arg(0),arg(1),arg(2),false),3);}
void regOpenKeyExA(){ret(registryOpen(arg(0),arg(1),arg(4),false),5);}
void regCreateKeyA(){ret(registryOpen(arg(0),arg(1),arg(2),true),3);}
void regCreateKeyExA(){
    if(arg(8))write32(arg(8),1);  // REG_CREATED_NEW_KEY: no se distingue y nadie lo mira
    ret(registryOpen(arg(0),arg(1),arg(7),true),9);
}
void regCloseKey(){ret(registryHandles.erase(arg(0))||arg(0)>=0x80000000u&&arg(0)<=0x80000005u?0:6,1);}
uint32_t registryQuery(const std::string& key,const std::string& name,uint32_t typeOut,uint32_t dataOut,uint32_t sizeOut){
    auto values=registryKeys.find(key);if(values==registryKeys.end())return 2;
    auto value=values->second.find(name);if(value==values->second.end())return 2;
    uint32_t bytes=uint32_t(value->second.bytes.size());
    if(typeOut)write32(typeOut,value->second.type);
    if(sizeOut){
        uint32_t capacity=read32(sizeOut);write32(sizeOut,bytes);
        if(dataOut){if(capacity<bytes)return 234;if(bytes)std::memcpy(ptr(dataOut,bytes),value->second.bytes.data(),bytes);}
    }else if(dataOut)return 87;
    return 0;
}
void regQueryValueExA(){
    auto key=registryRoot(arg(0));if(key.empty()){ret(6,6);return;}
    ret(registryQuery(key,arg(1)?lower(guestString(arg(1))):std::string("@"),arg(3),arg(4),arg(5)),6);
}
void regQueryValueA(){
    auto key=registryRoot(arg(0));if(key.empty()){ret(6,4);return;}
    if(arg(1)&&*static_cast<char*>(ptr(arg(1),1)))key+="\\"+lower(guestString(arg(1)));
    ret(registryQuery(key,"@",0,arg(2),arg(3)),4);
}
void regSetValueExA(){
    auto key=registryRoot(arg(0));if(key.empty()){ret(6,6);return;}
    uint32_t size=arg(5);if(size>1048576){ret(87,6);return;}
    registrySet(key,arg(1)?lower(guestString(arg(1))):std::string("@"),arg(3),size?ptr(arg(4),size):nullptr,size);
    saveRegistry();ret(0,6);
}
void regSetValueA(){
    auto key=registryRoot(arg(0));if(key.empty()){ret(6,5);return;}
    if(arg(1)&&*static_cast<char*>(ptr(arg(1),1)))key+="\\"+lower(guestString(arg(1)));
    auto text=guestString(arg(3));registrySet(key,"@",1,text.c_str(),text.size()+1);saveRegistry();ret(0,5);
}
void regDeleteValueA(){
    auto key=registryRoot(arg(0));auto values=registryKeys.find(key);
    if(values==registryKeys.end()||!values->second.erase(lower(guestString(arg(1))))){ret(2,2);return;}
    saveRegistry();ret(0,2);
}
void regDeleteKeyA(){
    auto key=registryRoot(arg(0));if(key.empty()){ret(6,2);return;}
    key+="\\"+lower(guestString(arg(1)));
    if(!registryKeys.erase(key)){ret(2,2);return;}
    saveRegistry();ret(0,2);
}
void regEnumKeyA(){
    auto key=registryRoot(arg(0))+"\\";uint32_t index=arg(1),out=arg(2),capacity=arg(3),at=0;
    for(auto& [path,values]:registryKeys){
        if(path.rfind(key,0)!=0||path.find('\\',key.size())!=std::string::npos)continue;
        if(at++!=index)continue;
        auto name=path.substr(key.size());
        if(capacity<=name.size()){ret(234,4);return;}
        std::memcpy(ptr(out,name.size()+1),name.c_str(),name.size()+1);ret(0,4);return;
    }
    ret(259,4);  // ERROR_NO_MORE_ITEMS
}
void regFlushKey(){ret(0,1);}
const std::map<std::string,recomp_func_t> registryFunctions={
    {"RegOpenKeyA",regOpenKeyA},{"RegOpenKeyExA",regOpenKeyExA},{"RegCreateKeyA",regCreateKeyA},{"RegCreateKeyExA",regCreateKeyExA},
    {"RegCloseKey",regCloseKey},{"RegQueryValueExA",regQueryValueExA},{"RegQueryValueA",regQueryValueA},{"RegSetValueExA",regSetValueExA},
    {"RegSetValueA",regSetValueA},{"RegDeleteValueA",regDeleteValueA},{"RegDeleteKeyA",regDeleteKeyA},{"RegEnumKeyA",regEnumKeyA},{"RegFlushKey",regFlushKey},
};
