#pragma once
#ifdef __ANDROID__
#include <android/log.h>
#define NFS_RUNTIME_LOG __android_log_print
#else
#include <cstdio>
#include <cstdarg>
constexpr int ANDROID_LOG_INFO = 4, ANDROID_LOG_WARN = 5, ANDROID_LOG_ERROR = 6;
inline int runtimeLog(int, const char* tag, const char* format, ...) {
    std::fprintf(stderr,"[%s] ",tag);
    va_list args; va_start(args,format);
    int result=std::vfprintf(stderr,format,args); va_end(args);
    std::fputc('\n',stderr); return result;
}
#define NFS_RUNTIME_LOG runtimeLog
#endif
