#pragma once
#include "Types.hpp"
#include <string_view>

namespace resource
{
    class FontResource
    {
    public:
        static FontData loadFont(std::string_view path);
    };
}
