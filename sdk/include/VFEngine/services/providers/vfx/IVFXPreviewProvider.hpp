#pragma once

#include "../PreviewInstanceId.hpp"
#include <vfx/VFXModifierTypes.hpp>
#include <vfx/VFXForceTypes.hpp>
#include <vfx/VFXShapeTypes.hpp>
#include <vfx/VFXEventTypes.hpp>
#include <vfx/VFXBurstTypes.hpp>
#include <vector>
#include <glm/glm.hpp>
#include <string>
#include <utility>
#include <cstdint>

namespace services
{
    struct VFXPreviewParams
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
        std::vector<::vfx::VFXBurst> bursts;

        int flipbookRows = 1;
        int flipbookColumns = 1;
        float flipbookFrameRate = 0.0f;
        bool flipbookRandomStart = false;

        float alphaClipThreshold = 0.1f;
        bool additiveBlend = false;

        int renderMode = 0;
        float softParticleDistance = 0.0f;
        float stretchMultiplier = 1.0f;

        std::string meshPath;

        int maxTrailPoints = 64;
        float ribbonWidth = 1.0f;
        float ribbonMinDistance = 0.1f;

        float uvScrollSpeedU = 0.0f;
        float uvScrollSpeedV = 0.0f;

        ::vfx::VFXEventConfig events;

        // Lighting
        float lightingInfluence = 0.0f;
        int normalMode = 0;
        float ambientAmount = 0.3f;

        bool collisionEnabled = false;
        float collisionBounce = 0.5f;
        float collisionFriction = 0.1f;
        float collisionLifetimeLoss = 0.0f;
    };

    // VK-1451 — one step of a composited sequence preview: a fully-built emitter
    // (params), where it sits (localTransform), its deterministic seed, and the
    // timing the embedded schedule needs to spawn/stop/cue it.
    struct VFXSequencePreviewStep
    {
        VFXPreviewParams params;
        glm::mat4 localTransform{1.0f};
        uint32_t seed = 0;
        float startTime = 0.0f;
        float duration = 0.0f;
        bool loop = false;
        int stopMode = 0; // 0 = PlayToCompletion, 1 = StopAfterDuration
        std::string cueName; // empty => time-driven
    };

    struct VFXSequencePreviewDesc
    {
        std::vector<VFXSequencePreviewStep> steps;
        std::vector<std::pair<float, std::string>> markers; // {time, cueName}
        uint32_t seed = 0;
        float playbackRate = 1.0f;
        float fixedStep = 0.0f; // 0 => variable step
    };

    class IVFXPreviewProvider
    {
    public:
        virtual ~IVFXPreviewProvider() = default;

        virtual void initVFXPreview(PreviewInstanceId instanceId) = 0;

        virtual void cleanUpVFXPreview(PreviewInstanceId instanceId) = 0;

        virtual void setVFXParams(PreviewInstanceId instanceId, const VFXPreviewParams& params) = 0;

        virtual void updateVFXCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                     const glm::mat4& projection, const glm::vec3& cameraPos,
                                     float time) = 0;

        virtual void updateVFXSimulation(PreviewInstanceId instanceId, float deltaTime) = 0;

        virtual void playVFX(PreviewInstanceId instanceId) = 0;

        virtual void pauseVFX(PreviewInstanceId instanceId) = 0;

        virtual void stopVFX(PreviewInstanceId instanceId) = 0;

        virtual void* renderVFXPreview(PreviewInstanceId instanceId) = 0;

        // VK-1451 — composited sequence preview. Default no-ops so non-sequence
        // providers/mocks need not implement them; play/pause/stop/updateSimulation/
        // render are reused as-is for the sequence path.
        virtual void setVFXSequence(PreviewInstanceId, const VFXSequencePreviewDesc&) {}
        virtual void seekVFX(PreviewInstanceId, float) {}
        virtual void setVFXRate(PreviewInstanceId, float) {}
    };
}
