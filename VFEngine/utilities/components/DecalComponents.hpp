#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include "../asset/AssetRef.hpp"

namespace components
{
    enum class DecalShape : uint8_t
    {
        Rectangle = 0,
        Circle = 1,
        Triangle = 2
    };

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
