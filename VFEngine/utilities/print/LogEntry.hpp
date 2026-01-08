#pragma once
#include <string>
#include <cstdint>

namespace util
{
    enum class LogLevel : uint8_t
    {
        Info = 0,
        Warning = 1,
        Error = 2
    };

    struct LogEntry
    {
        std::string message;
        LogLevel level;
        uint64_t sequenceNumber;

        LogEntry(std::string msg, LogLevel lvl, uint64_t seq)
            : message(std::move(msg)), level(lvl), sequenceNumber(seq)
        {
        }
    };
}
