#pragma once
#include <string>
#include <functional>

namespace util {
    // Optional debug log callback, set by runtime Main.cpp to write to crash log file.
    // Null by default (no-op). Safe to call from any module.
    inline std::function<void(const std::string&)> runtimeDebugLogFn = nullptr;

    inline void runtimeDebugLog(const std::string& msg) {
        if (runtimeDebugLogFn) {
            runtimeDebugLogFn(msg);
        }
    }
}
