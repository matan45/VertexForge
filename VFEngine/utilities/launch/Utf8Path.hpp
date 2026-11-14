#pragma once

#include <filesystem>
#include <string>

namespace launch
{
    // Launch messages (stderr, the launch status file) are UTF-8. A narrow path string on
    // Windows is in the ANSI code page, so always convert through u8string.
    inline std::string pathToUtf8(const std::filesystem::path& path)
    {
        const std::u8string utf8 = path.u8string();
        return std::string(utf8.begin(), utf8.end());
    }
}
