#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>

namespace dn {
namespace {

std::mutex g_mutex;
std::unordered_map<std::string, ULONGLONG> g_lastByKey;
LogSink g_sink = nullptr;

void writeLine(const char* fmt, va_list ap) {
    if (!g_sink) return;
    char line[1024];
    vsnprintf(line, sizeof(line), fmt, ap);
    g_sink(line);
}

}  // namespace

void logToSink(LogSink sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sink = sink;
}

void logf(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_mutex);
    va_list ap;
    va_start(ap, fmt);
    writeLine(fmt, ap);
    va_end(ap);
}

void logRateLimited(const char* key, unsigned intervalMs, const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_mutex);
    ULONGLONG now = GetTickCount64();
    auto it = g_lastByKey.find(key);
    if (it != g_lastByKey.end() && now - it->second < intervalMs) return;
    g_lastByKey[key] = now;
    va_list ap;
    va_start(ap, fmt);
    writeLine(fmt, ap);
    va_end(ap);
}

}  // namespace dn
