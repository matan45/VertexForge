#pragma once
#include "Types.hpp"
#include "VfFontHeader.hpp"
#include <string_view>

namespace resource
{
    class FontResource
    {
    public:
        static FontData loadFont(std::string_view path);

        // Reads only the .vfFont header and says whether this engine can load it.
        // Cheap enough for the content browser to call while a context menu is open,
        // and the only way for the editor to tell "this asset needs re-importing" from
        // "this asset is fine" — loadFont's failure is otherwise invisible, because the
        // renderer substitutes the built-in default font and carries on (VK-1628).
        [[nodiscard]] static FontHeaderStatus probeHeader(std::string_view path);
    };
}
