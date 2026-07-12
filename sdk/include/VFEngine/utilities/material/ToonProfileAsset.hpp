#pragma once
#include "ToonProfile.hpp"
#include <string>
#include <string_view>
#include <optional>

namespace material
{
    // JSON (de)serializer for the `.vfToonProfile` asset. Modeled on MaterialAsset:
    // per-field `j.value(key, default)` defaulting, no version shims (matches the
    // no-backward-compat convention). References no other assets.
    class ToonProfileAsset
    {
    public:
        static constexpr const char* FORMAT_VERSION = "1.0";

        static bool save(std::string_view path, const ToonProfile& profile);
        static std::optional<ToonProfile> load(std::string_view path);
    };
}
