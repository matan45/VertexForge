#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include "../asset/AssetRef.hpp"

namespace components
{
    enum class DecalShape : uint8_t
    {
        Rectangle = 0,
        Circle = 1,
        Triangle = 2
    };

    // Single source of truth for DecalShape's count and display/serialization names.
    // Anything that clamps a raw shape index or maps to/from a name must derive from
    // here — adding a shape means editing only this enum + the name table (+ the shader).
    inline constexpr uint8_t kDecalShapeCount = 3;
    inline constexpr const char* kDecalShapeNames[kDecalShapeCount] = { "Rectangle", "Circle", "Triangle" };

    // Clamp an arbitrary integer to a valid DecalShape (out-of-range -> Rectangle).
    inline constexpr DecalShape toDecalShape(int64_t value)
    {
        return (value >= 0 && value < kDecalShapeCount)
                   ? static_cast<DecalShape>(value)
                   : DecalShape::Rectangle;
    }

    inline const char* decalShapeName(DecalShape shape)
    {
        const auto idx = static_cast<uint8_t>(shape);
        return idx < kDecalShapeCount ? kDecalShapeNames[idx] : kDecalShapeNames[0];
    }

    // Case-insensitive name -> shape (unknown -> Rectangle).
    inline DecalShape decalShapeFromName(std::string_view name)
    {
        const auto iequals = [](std::string_view a, std::string_view b)
        {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i)
            {
                const char ca = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
                const char cb = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
                if (ca != cb) return false;
            }
            return true;
        };
        for (uint8_t i = 0; i < kDecalShapeCount; ++i)
            if (iequals(name, kDecalShapeNames[i])) return static_cast<DecalShape>(i);
        return DecalShape::Rectangle;
    }

    struct DecalComponent
    {
        DecalShape shape = DecalShape::Rectangle;
        glm::vec3 halfExtents{0.5f, 0.5f, 0.1f};
        asset::AssetRef albedoTextureRef;
        asset::AssetRef normalTextureRef;
        asset::AssetRef ormTextureRef;
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        float angleFadeStart = 0.7f;
        float angleFadeEnd = 0.3f;
        float edgeFalloff = 0.1f;
        int32_t sortPriority = 0;
        bool modifyNormals = true;
        float normalStrength = 1.0f;
    };
}
