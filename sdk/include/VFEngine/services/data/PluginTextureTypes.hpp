#pragma once
#include <cstdint>
#include <string>
#include <glm/glm.hpp>

namespace plugin {

    struct PluginTextureHandle {
        uint64_t id = 0;
        bool isValid() const { return id != 0; }
    };

    // Texel formats for plugin-owned 2D textures. Data passed to
    // updateTexture2D is tightly packed rows of width * bytesPerPixel.
    enum class TextureFormat : uint32_t {
        R8    = 0,   // 1 byte/texel  -> vk::Format::eR8Unorm
        RGBA8 = 1    // 4 bytes/texel -> vk::Format::eR8G8B8A8Unorm
    };

    inline constexpr uint32_t textureFormatBytesPerPixel(TextureFormat format) {
        return format == TextureFormat::RGBA8 ? 4u : 1u;
    }

    // VK-1488: deterministic UI external-texture key for a plugin texture handle.
    // Single source of truth for the "__plugintex_<id>__" convention used to bind
    // a plugin/GPU texture to UIImageComponent.externalTextureKey. Deterministic
    // so a caller can recompute it without a round-trip; also returned by
    // IPluginTextureProvider::registerUITexture / PluginContext::registerUITexture.
    inline std::string pluginTextureUIKey(const PluginTextureHandle& handle) {
        return "__plugintex_" + std::to_string(handle.id) + "__";
    }

    // World-space XZ-projected mask behavior. All effect fields default to
    // "no effect" so a freshly bound mask does nothing until configured.
    // The mask texture's red channel is sampled with UV derived from
    // fragWorldPos.xz mapped over [worldMin.xz, worldMax.xz]; fragments
    // outside the bounds are unaffected (treated as mask = 1.0).
    struct WorldMaskParams {
        bool  enabled            = true;   // runtime gate; false => mask treated as 1.0
        bool  affectsTerrain     = false;  // terrain albedo *= mix(terrainDimMin, 1, mask)
        float terrainDimMin      = 0.25f;  // albedo multiplier where mask == 0
        bool  affectsEntities    = false;  // entities discard where mask < entityDiscardBelow
        float entityDiscardBelow = 0.5f;   // discard threshold [0,1]
        bool  affectsShadows     = false;  // fade terrain cast-shadows by the mask (hide ghost
                                           // shadows of fog-hidden casters); shadow=mix(1,shadow,mask)
    };

}
