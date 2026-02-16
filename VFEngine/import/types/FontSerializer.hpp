#pragma once
#include <string_view>
#include "resource/Types.hpp"

namespace types
{
    class FontSerializer
    {
    public:
        void saveToFile(std::string_view location, std::string_view fileName,
                        const resource::FontData& fontData) const;
    };
}
