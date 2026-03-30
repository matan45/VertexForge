#pragma once
#include <string>
#include <vector>
#include <functional>

namespace windows
{
    struct SettingsEntry
    {
        std::string name;
        std::string description;
        std::vector<std::string> tags;
        int category;
        std::function<void()> drawFunction;
    };
}
