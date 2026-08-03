#include "Logger.h"

#include <fstream>
#include <mutex>
#include <chrono>
#include <ctime>
#include <cstdio>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace masdr {
namespace {

std::ofstream& logFile() {
    static std::ofstream f;
    static bool opened = false;
    if (!opened) {
        opened = true;
        char exePath[MAX_PATH]{};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string dir(exePath);
        auto pos = dir.find_last_of("\\/");
        if (pos != std::string::npos) dir = dir.substr(0, pos);
        f.open(dir + "\\run.log", std::ios::out | std::ios::trunc);
    }
    return f;
}

std::mutex& logMutex() {
    static std::mutex m;
    return m;
}

std::string timestamp() {
    using namespace std::chrono;
    auto now   = system_clock::now();
    auto t     = system_clock::to_time_t(now);
    auto ms    = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    struct tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
             tm.tm_hour, tm.tm_min, tm.tm_sec, (int)ms.count());
    return buf;
}

void writeLog(const char* level, const std::string& s) {
    std::string line = timestamp() + " [" + level + "] " + s + "\n";
    std::lock_guard<std::mutex> lk(logMutex());
    auto& f = logFile();
    if (f.is_open()) {
        f << line;
        f.flush();
    }
    OutputDebugStringA(line.c_str());
}

} // namespace

void Logger::info (const std::string& s) { writeLog("INFO ", s); }
void Logger::warn (const std::string& s) { writeLog("WARN ", s); }
void Logger::error(const std::string& s) { writeLog("ERROR", s); }
void Logger::debug(const std::string& s) { writeLog("DEBUG", s); }

} // namespace masdr
