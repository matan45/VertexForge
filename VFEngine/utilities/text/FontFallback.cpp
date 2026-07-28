#include "FontFallback.hpp"
#include "../resource/DefaultFont.hpp"

#include <algorithm>
#include <cstddef>

namespace text
{
    namespace
    {
        [[nodiscard]] constexpr bool isTrimCharacter(char value) noexcept
        {
            return value == ' ' || value == '\t' || value == '\r' || value == '\n';
        }

        [[nodiscard]] std::string_view trimPath(std::string_view path) noexcept
        {
            while (!path.empty() && isTrimCharacter(path.front()))
            {
                path.remove_prefix(1);
            }
            while (!path.empty() && isTrimCharacter(path.back()))
            {
                path.remove_suffix(1);
            }
            return path;
        }

        [[nodiscard]] constexpr char canonicalSeparator(char value) noexcept
        {
            return value == '\\' ? '/' : value;
        }

        [[nodiscard]] bool equivalentPath(std::string_view lhs, std::string_view rhs) noexcept
        {
            lhs = trimPath(lhs);
            rhs = trimPath(rhs);
            if (lhs.size() != rhs.size())
            {
                return false;
            }

            for (std::size_t i = 0; i < lhs.size(); ++i)
            {
                if (canonicalSeparator(lhs[i]) != canonicalSeparator(rhs[i]))
                {
                    return false;
                }
            }
            return true;
        }
    }

    NormalizedFallbackChain normalizeFallbackChain(
        std::span<const std::string> authored,
        std::string_view primaryPath) noexcept
    {
        NormalizedFallbackChain result;
        const std::string_view defaultSentinel = resource::DEFAULT_FONT_SENTINEL;

        for (const std::string& authoredPath : authored)
        {
            if (result.count >= MAX_AUTHORED_FALLBACKS)
            {
                break;
            }

            const std::string_view path = trimPath(authoredPath);
            if (path.empty() || equivalentPath(path, primaryPath) ||
                equivalentPath(path, defaultSentinel))
            {
                continue;
            }

            bool duplicate = false;
            for (uint8_t i = 0; i < result.count; ++i)
            {
                if (equivalentPath(path, result.paths[i]))
                {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
            {
                result.paths[result.count++] = path;
            }
        }

        result.paths[result.count++] = defaultSentinel;
        return result;
    }
}
