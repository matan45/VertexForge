#pragma once

#include "PreviewInstanceId.hpp"
#include <vfx/VFXModifierTypes.hpp>
#include <vfx/VFXForceTypes.hpp>
#include <vfx/VFXShapeTypes.hpp>
#include <glm/glm.hpp>
#include <string>

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

        int flipbookRows = 1;
        int flipbookColumns = 1;
        float flipbookFrameRate = 0.0f;
        bool flipbookRandomStart = false;

        float alphaClipThreshold = 0.1f;
        bool additiveBlend = false;

        // Render mode & soft particles (VK-494)
        int renderMode = 0;
        float softParticleDistance = 0.0f;
        float stretchMultiplier = 1.0f;
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
    };
}
