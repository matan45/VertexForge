#pragma once
#include <string_view>
#include "../ImportExport.hpp"
#include "resource/Types.hpp"

namespace types
{
    class VF_IMPORT_API FontSerializer
    {
    public:
        void saveToFile(std::string_view location, std::string_view fileName,
                        const resource::FontData& fontData) const;
    };
}
