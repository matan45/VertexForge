#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <string>
#include "vfx/VFXModifierTypes.hpp"
#include "vfx/VFXForceTypes.hpp"
#include "vfx/VFXShapeTypes.hpp"

namespace render::vfx
{
    enum class VFXRenderMode : uint32_t
    {
        Billboard = 0,
        StretchedBillboard = 1,
        HorizontalBillboard = 2,
        MeshParticle = 3
    };

    struct VFXParticle
    {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec4 color{1.0f};
        float size = 1.0f;
        float rotation = 0.0f;
        float lifetime = 0.0f;
        float maxLifetime = 1.0f;
        bool active = false;

        glm::vec4 initialColor{1.0f};
        float initialSize = 1.0f;
        float initialSpeed = 1.0f;
        glm::vec3 initialDirection{0.0f, 1.0f, 0.0f};

        uint32_t spawnSeed = 0;
        float glowIntensity = 0.0f;
    };

    struct VFXInstanceData
    {
        glm::vec3 worldPosition;
        float size;
        glm::vec4 color;
        float lifetimeRatio;
        float rotation;
        float flipbookFrameIndex;
        float glowIntensity;

        static vk::VertexInputBindingDescription getBindingDescription()
        {
            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 1;
            bindingDescription.stride = sizeof(VFXInstanceData);
            bindingDescription.inputRate = vk::VertexInputRate::eInstance;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 6> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 6> attributes{};

            attributes[0].binding = 1;
            attributes[0].location = 2;
            attributes[0].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[0].offset = offsetof(VFXInstanceData, worldPosition);

            attributes[1].binding = 1;
            attributes[1].location = 3;
            attributes[1].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[1].offset = offsetof(VFXInstanceData, color);

            attributes[2].binding = 1;
            attributes[2].location = 4;
            attributes[2].format = vk::Format::eR32Sfloat;
            attributes[2].offset = offsetof(VFXInstanceData, lifetimeRatio);

            attributes[3].binding = 1;
            attributes[3].location = 5;
            attributes[3].format = vk::Format::eR32Sfloat;
            attributes[3].offset = offsetof(VFXInstanceData, rotation);

            attributes[4].binding = 1;
            attributes[4].location = 6;
            attributes[4].format = vk::Format::eR32Sfloat;
            attributes[4].offset = offsetof(VFXInstanceData, flipbookFrameIndex);

            attributes[5].binding = 1;
            attributes[5].location = 7;
            attributes[5].format = vk::Format::eR32Sfloat;
            attributes[5].offset = offsetof(VFXInstanceData, glowIntensity);

            return attributes;
        }
    };

    struct VFXQuadVertex
    {
        glm::vec2 position;
        glm::vec2 texCoord;

        static vk::VertexInputBindingDescription getBindingDescription()
        {
            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(VFXQuadVertex);
            bindingDescription.inputRate = vk::VertexInputRate::eVertex;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 2> attributes{};

            attributes[0].binding = 0;
            attributes[0].location = 0;
            attributes[0].format = vk::Format::eR32G32Sfloat;
            attributes[0].offset = offsetof(VFXQuadVertex, position);

            attributes[1].binding = 0;
            attributes[1].location = 1;
            attributes[1].format = vk::Format::eR32G32Sfloat;
            attributes[1].offset = offsetof(VFXQuadVertex, texCoord);

            return attributes;
        }
    };

    struct VFXCameraUBO
    {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 projection;
        alignas(16) glm::vec3 cameraPos;
        alignas(4) float time;
    };

    struct VFXEmitterConfig
    {
        float spawnRate = 10.0f;
        float lifetime = 2.0f;
        float startSize = 1.0f;
        float startSpeed = 1.0f;
        glm::vec4 startColor{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec3 emitDirection{0.0f, 1.0f, 0.0f};
        std::string texturePath;
        bool looping = true;

        ::vfx::VFXModifierChain modifiers;
        ::vfx::VFXForceChain forces;
        ::vfx::ShapeConfig shape;

        int flipbookRows = 1;
        int flipbookColumns = 1;
        float flipbookFrameRate = 0.0f;
        bool flipbookRandomStart = false;

        float alphaClipThreshold = 0.1f;
        bool additiveBlend = false;

        // Render mode & soft particles (VK-494)
        VFXRenderMode renderMode = VFXRenderMode::Billboard;
        float softParticleDistance = 0.0f;
        float stretchMultiplier = 1.0f;

        // Mesh particle (VK-496)
        std::string meshPath;

        // Glow color
        glm::vec3 glowColor{1.0f, 1.0f, 1.0f};
    };

    namespace VFXConstants
    {
        inline constexpr size_t MAX_PARTICLES = 1000;
        inline constexpr uint32_t QUAD_INDEX_COUNT = 6;
    }
}
