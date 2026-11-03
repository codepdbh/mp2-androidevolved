#pragma once
#include <cstdio>
#include <sys/stat.h>
#include <string>
#include <stdexcept>
#include <android/log.h>

constexpr const char* kGameRoot = "/storage/emulated/0/mp2";
inline void checkGameData() {
    const std::string root(kGameRoot);
    FILE* exe = std::fopen((root + "/MaxPayne2.exe").c_str(), "rb");
    if (!exe) throw std::runtime_error("No se puede leer mp2/MaxPayne2.exe en la memoria interna: copia ahí tu instalación y concede el permiso de archivos");
    unsigned char magic[2]{};
    const size_t read = std::fread(magic, 1, 2, exe);
    std::fclose(exe);
    if (read != 2 || magic[0] != 'M' || magic[1] != 'Z') throw std::runtime_error("MaxPayne2.exe no es un ejecutable válido");
    for (const char* name : {"MP2_Data.ras", "MP2_Init.ras", "e2mfc.dll", "e2driver/e2_d3d8_driver_mfc.dll"}) {
        struct stat info{};
        if (stat((root + "/" + name).c_str(), &info)) throw std::runtime_error(std::string("Falta en la carpeta mp2: ") + name);
    }
    __android_log_print(ANDROID_LOG_INFO, "NFSC", "Datos del juego accesibles en %s", kGameRoot);
}
