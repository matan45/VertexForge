#pragma once

#include <filesystem>
#include <string>

namespace launch
{
    // Launch messages (stderr, the launch status file) are UTF-8. A narrow path string on
    // Windows is in the process code page (UTF-8 under the VK-1684 manifest, the ANSI code
    // page on Windows older than 10 1903), so always convert through u8string.
    inline std::string pathToUtf8(const std::filesystem::path& path)
    {
        const std::u8string utf8 = path.u8string();
        return std::string(utf8.begin(), utf8.end());
    }
}
