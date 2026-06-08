#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace services
{
    struct DecalRenderData
    {
        glm::mat4 worldMatrix{1.0f};
        glm::mat4 inverseWorldMatrix{1.0f};
        glm::vec3 halfExtents{0.5f, 0.5f, 0.1f};
        uint32_t shape = 0; // 0=Rectangle, 1=Circle, 2=Triangle
        std::string albedoTexture;
        std::string normalTexture;
        std::string ormTexture;
        glm::vec4 color{1.0f};
        float angleFadeStart = 0.7f;
        float angleFadeEnd = 0.3f;
        float edgeFalloff = 0.1f;
        int32_t sortPriority = 0;
        bool modifyNormals = true;
        float normalStrength = 1.0f;
    };

    class IDecalRenderProvider
    {
    public:
        virtual ~IDecalRenderProvider() = default;

        virtual void setDecalRenderingEnabled(bool enabled) = 0;
        virtual bool isDecalRenderingEnabled() const = 0;
    };
}
