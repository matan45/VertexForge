#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "../PreviewInstanceId.hpp"
#include "../render/IMeshPreviewProvider.hpp"
#include "retargeting/RetargetTypes.hpp"
#include <string>
#include <vector>

namespace services
{
    // Evaluated bone transform info for UI display
    struct EvaluatedBoneInfo
    {
        std::string name;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
        glm::mat4 worldTransform{1.0f};
        glm::vec3 skinnedPosition{0.0f};  // Final position after skinning (for visualization)
        int32_t parentIndex = -1;
    };

    // Animation playback parameters
    struct AnimationPreviewParams
    {
        glm::mat4 modelMatrix{1.0f};
        glm::vec4 albedo{0.8f, 0.8f, 0.8f, 1.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;
        glm::vec4 clearColor{0.06f, 0.06f, 0.06f, 1.0f};
    };

    class IAnimationPreviewProvider
    {
    public:
        virtual ~IAnimationPreviewProvider() = default;

        virtual void initAnimationPreview(PreviewInstanceId instanceId) = 0;
        virtual void cleanUpAnimationPreview(PreviewInstanceId instanceId) = 0;
        virtual bool loadAnimationPreviewMesh(PreviewInstanceId instanceId, const std::string& meshPath) = 0;
        virtual bool loadAnimationPreviewAnimation(PreviewInstanceId instanceId, const std::string& animPath) = 0;
        // VK-910: play a source clip retargeted onto the loaded (target) mesh's skeleton.
        virtual bool loadRetargetedAnimationPreview(PreviewInstanceId instanceId,
                                                    const std::string& sourceAnimPath,
                                                    const std::string& sourceMeshPath,
                                                    const retargeting::HumanoidRigData& sourceRig,
                                                    const retargeting::HumanoidRigData& targetRig,
                                                    const retargeting::RetargetMapData& map) = 0;

        virtual void playAnimation(PreviewInstanceId instanceId) = 0;
        virtual void pauseAnimation(PreviewInstanceId instanceId) = 0;
        virtual void stopAnimation(PreviewInstanceId instanceId) = 0;
        virtual bool isAnimationPlaying(PreviewInstanceId instanceId) const = 0;

        virtual void setAnimationPlaybackTime(PreviewInstanceId instanceId, float timeSeconds) = 0;
        virtual float getAnimationPlaybackTime(PreviewInstanceId instanceId) const = 0;

        virtual void setAnimationLooping(PreviewInstanceId instanceId, bool loop) = 0;
        virtual void setAnimationPlaybackSpeed(PreviewInstanceId instanceId, float speed) = 0;

        virtual void updateAnimationPreview(PreviewInstanceId instanceId, float deltaTime) = 0;
        virtual void setAnimationPreviewParams(PreviewInstanceId instanceId, const AnimationPreviewParams& params) = 0;
        virtual void setAnimationPreviewEnvironment(PreviewInstanceId instanceId,
                                                    const PreviewEnvironmentParams& params) = 0;
        virtual void updateAnimationCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                           const glm::mat4& projection, const glm::vec3& cameraPos) = 0;

        virtual void* renderAnimationPreview(PreviewInstanceId instanceId) = 0;
        virtual std::vector<EvaluatedBoneInfo> getAnimationPreviewEvaluatedBones(PreviewInstanceId instanceId) const = 0;
    };
}
