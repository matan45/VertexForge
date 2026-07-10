#pragma once
#include <string_view>

namespace render::ui
{
    // A "synthetic" UI texture key names a live GPU texture registered into the UI
    // bindless external-texture table (an RTT target or, VK-1488, a plugin/GPU
    // texture) rather than a file asset path. The record path frame-skips an
    // unregistered synthetic key (the source isn't ready this frame), whereas an
    // unresolved file path falls back to the white default at bindless index 0.
    //
    // The reserved prefixes MUST be listed explicitly: a blanket "__" test would
    // also match the default "__white_1x1__" texture (bindless index 0) and make
    // every plain colored quad frame-skip and vanish.
    [[nodiscard]] inline bool isSyntheticUITextureKey(std::string_view path) noexcept
    {
        return path.starts_with("__rtt_") || path.starts_with("__plugintex_");
    }
}
