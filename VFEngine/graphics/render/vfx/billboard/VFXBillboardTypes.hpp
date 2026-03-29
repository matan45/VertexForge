#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <string>
#include "vfx/VFXModifierTypes.hpp"
#include "vfx/VFXForceTypes.hpp"
#include "../../common/CameraTypes.hpp"
#include "vfx/VFXShapeTypes.hpp"
#include "vfx/VFXEventTypes.hpp"

namespace render::vfx
{
    enum class VFXRenderMode : uint32_t
    {
        Billboard = 0,
        StretchedBillboard = 1,
        HorizontalBillboard = 2,
        MeshParticle = 3,
        Ribbon = 4
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

    using VFXCameraUBO = render::common::CameraUBO;

    struct VFXRibbonSegmentData
    {
        glm::vec3 posA;
        float sizeA;
        glm::vec3 posB;
        float sizeB;
        glm::vec4 colorA;
        glm::vec4 colorB;
        float trailT;
        float glowIntensityA;
        float glowIntensityB;
        float _pad;

        static vk::VertexInputBindingDescription getBindingDescription()
        {
            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 1;
            bindingDescription.stride = sizeof(VFXRibbonSegmentData);
            bindingDescription.inputRate = vk::VertexInputRate::eInstance;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 7> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 7> attributes{};

            // location 2: posA + sizeA (vec4)
            attributes[0].binding = 1;
            attributes[0].location = 2;
            attributes[0].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[0].offset = offsetof(VFXRibbonSegmentData, posA);

            // location 3: posB + sizeB (vec4)
            attributes[1].binding = 1;
            attributes[1].location = 3;
            attributes[1].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[1].offset = offsetof(VFXRibbonSegmentData, posB);

            // location 4: colorA (vec4)
            attributes[2].binding = 1;
            attributes[2].location = 4;
            attributes[2].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[2].offset = offsetof(VFXRibbonSegmentData, colorA);

            // location 5: colorB (vec4)
            attributes[3].binding = 1;
            attributes[3].location = 5;
            attributes[3].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[3].offset = offsetof(VFXRibbonSegmentData, colorB);

            // location 6: trailT (float)
            attributes[4].binding = 1;
            attributes[4].location = 6;
            attributes[4].format = vk::Format::eR32Sfloat;
            attributes[4].offset = offsetof(VFXRibbonSegmentData, trailT);

            // location 7: glowIntensityA (float)
            attributes[5].binding = 1;
            attributes[5].location = 7;
            attributes[5].format = vk::Format::eR32Sfloat;
            attributes[5].offset = offsetof(VFXRibbonSegmentData, glowIntensityA);

            // location 8: glowIntensityB (float)
            attributes[6].binding = 1;
            attributes[6].location = 8;
            attributes[6].format = vk::Format::eR32Sfloat;
            attributes[6].offset = offsetof(VFXRibbonSegmentData, glowIntensityB);

            return attributes;
        }
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
        
        VFXRenderMode renderMode = VFXRenderMode::Billboard;
        float softParticleDistance = 0.0f;
        float stretchMultiplier = 1.0f;
        
        std::string meshPath;
        
        uint32_t maxTrailPoints = 64;
        float ribbonWidth = 1.0f;
        float ribbonMinDistance = 0.1f;
        
        float uvScrollSpeedU = 0.0f;
        float uvScrollSpeedV = 0.0f;
        
        glm::vec3 glowColor{1.0f, 1.0f, 1.0f};

        ::vfx::VFXEventConfig events;

        // Lighting
        float lightingInfluence = 0.0f;
        int normalMode = 0;
        float ambientAmount = 0.3f;

        bool collisionEnabled = false;
        float collisionBounce = 0.5f;
        float collisionFriction = 0.1f;
        float collisionLifetimeLoss = 0.0f;

        // Distortion
        bool distortionEnabled = false;
        float distortionStrength = 0.1f;
        std::string distortionTexturePath;
    };

    struct VFXFlipbookConfig
    {
        int rows = 1;
        int columns = 1;
        float alphaClipThreshold = 0.1f;
        bool additiveBlend = false;
        VFXRenderMode renderMode = VFXRenderMode::Billboard;
        float stretchMultiplier = 1.0f;
        glm::vec3 glowColor{1.0f};
        float uvScrollSpeedU = 0.0f;
        float uvScrollSpeedV = 0.0f;
    };

    namespace VFXConstants
    {
        inline constexpr size_t MAX_PARTICLES = 1000;
        inline constexpr uint32_t QUAD_INDEX_COUNT = 6;
    }
}
