#pragma once
#include <glm/glm.hpp>
#include <string>

namespace components
{
    struct DecalComponent
    {
        glm::vec3 halfExtents{0.5f, 0.5f, 0.1f};
        std::string albedoTexture;
        std::string normalTexture;
        std::string ormTexture;
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        float angleFadeStart = 0.7f;
        float angleFadeEnd = 0.3f;
        float edgeFalloff = 0.1f;
        int32_t sortPriority = 0;
        bool modifyNormals = true;
        float normalStrength = 1.0f;
    };
}
