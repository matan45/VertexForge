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
#include "vfx/VFXBurstTypes.hpp"
#include "vfx/VFXScalability.hpp"
#include "vfx/VFXBlendMode.hpp"

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
        float angularVelocity = 0.0f;
        float colorValueMult = 1.0f;
        float alphaMult = 1.0f;
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
        float coneSpread = 0.5f;  // radians, cone half-angle for emission direction
        std::string texturePath;
        bool looping = true;
        // Fraction of the emitter's own world velocity passed to new particles (0..1)
        float inheritVelocityRatio = 0.0f;

        // Spawn variance. Fraction fields are 0..1; rotation fields are radians.
        float sizeVariance = 0.0f;
        float lifetimeVariance = 0.0f;
        float speedVariance = 0.0f;
        float rotationVariance = 0.0f;
        float angularVelocityVariance = 0.0f;
        float colorValueVariance = 0.0f;
        float alphaVariance = 0.0f;

        ::vfx::VFXModifierChain modifiers;
        ::vfx::VFXForceChain forces;
        ::vfx::ShapeConfig shape;
        std::vector<::vfx::VFXBurst> bursts;

        int flipbookRows = 1;
        int flipbookColumns = 1;
        float flipbookFrameRate = 0.0f;
        bool flipbookRandomStart = false;
        bool flipbookFrameBlend = false; // VK-1469: linear crossfade between current and next cell

        float alphaClipThreshold = 0.1f;
        ::vfx::VFXBlendMode blendMode = ::vfx::VFXBlendMode::Alpha; // VK-1472 (replaces legacy additiveBlend bool)

        // VK-1471: per-emitter draw-order key. Emitter draws are stable-sorted by
        // sortOrder ascending within each render pipeline (lower = drawn behind).
        // Default 0 leaves draw order unchanged. Host-only — not sent to the GPU config.
        int sortOrder = 0;

        VFXRenderMode renderMode = VFXRenderMode::Billboard;
        float softParticleDistance = 0.0f;
        float stretchMultiplier = 1.0f;
        
        std::string meshPath;
        
        uint32_t maxTrailPoints = 64;
        float ribbonWidth = 1.0f;
        float ribbonMinDistance = 0.1f;

        // VK-1474: optional over-trail curves (normalized trail position, head=0 -> tail=1).
        // The bool gates activation: false => flat legacy behavior (channel/flag off, flat width).
        // When present the curve/gradient MULTIPLY the flat ribbonWidth / particle color.
        ::vfx::VFXCurve ribbonWidthCurve;
        ::vfx::VFXGradient ribbonTailGradient;
        bool hasRibbonWidthCurve = false;
        bool hasRibbonTailGradient = false;

        float uvScrollSpeedU = 0.0f;
        float uvScrollSpeedV = 0.0f;
        
        glm::vec3 glowColor{1.0f, 1.0f, 1.0f};
        float emissiveIntensity = 1.0f;

        ::vfx::VFXEventConfig events;

        // Lighting
        float lightingInfluence = 0.0f;
        int normalMode = 0;
        float ambientAmount = 0.3f;

        // Proxy light emission (instance illuminates the scene as a point light)
        bool lightEmissionEnabled = false;
        float lightEmissionIntensity = 5.0f;
        float lightEmissionRadius = 10.0f;

        bool collisionEnabled = false;
        float collisionBounce = 0.5f;
        float collisionFriction = 0.1f;
        float collisionLifetimeLoss = 0.0f;

        // Distortion
        bool distortionEnabled = false;
        float distortionStrength = 0.1f;
        std::string distortionTexturePath;

        // VK-1453 (Phase 4) — per-quality-tier scalability profile (CPU-only; disabled
        // by default so the resolved level is neutral and runtime behavior is unchanged).
        ::vfx::VFXScalability scalability;
    };

    struct VFXFlipbookConfig
    {
        int rows = 1;
        int columns = 1;
        float alphaClipThreshold = 0.1f;
        ::vfx::VFXBlendMode blendMode = ::vfx::VFXBlendMode::Alpha; // VK-1472 (replaces legacy additiveBlend bool)
        VFXRenderMode renderMode = VFXRenderMode::Billboard;
        float stretchMultiplier = 1.0f;
        glm::vec3 glowColor{1.0f};
        float emissiveIntensity = 1.0f;
        float uvScrollSpeedU = 0.0f;
        float uvScrollSpeedV = 0.0f;
        bool frameBlend = false;   // VK-1469: crossfade current->next flipbook cell
        float frameRate = 0.0f;    // needed to resolve loop (wrap) vs clamp (one-shot) mode
    };

    // Resolves the per-emitter frame-blend push-constant mode for the CPU-sim preview
    // path: 0 = off (discrete), 1 = loop (wrap last->first), 2 = clamp (one-shot, hold
    // last). Mirrors the GPU-sim shader's loop rule (loop == frameRate > 0).
    inline uint32_t frameBlendModeFor(bool enabled, float frameRate)
    {
        if (!enabled)
            return 0u;
        return frameRate > 0.0f ? 1u : 2u;
    }

    namespace VFXConstants
    {
        inline constexpr size_t MAX_PARTICLES = 1000;
        inline constexpr uint32_t QUAD_INDEX_COUNT = 6;
    }
}
