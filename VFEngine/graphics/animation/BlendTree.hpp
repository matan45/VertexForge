#pragma once

#include "animator/BlendTreeTypes.hpp"
#include "animator/AnimatorTypes.hpp"
#include "AnimationEvaluator.hpp"
#include "AnimationBlender.hpp"
#include "resource/Types.hpp"
#include <vector>
#include <functional>
#include <glm/glm.hpp>

namespace animation
{
    using AnimationLoadCallback = std::function<const resource::AnimationData*(const std::string& path)>;

    class BlendTreeEvaluator
    {
    public:
        std::vector<glm::mat4> evaluate(
            const animator::BlendTreeData& blendTree,
            const animator::AnimatorRuntimeParameters& params,
            const resource::SkeletonData& skeleton,
            float stateTime,
            const AnimationLoadCallback& loadCallback) const;

        std::vector<glm::mat4> evaluate(
            const animator::BlendTreeData& blendTree,
            const animator::AnimatorRuntimeParameters& params,
            const resource::SkeletonData& skeleton,
            float stateTime,
            const AnimationLoadCallback& loadCallback,
            glm::vec3& outRootPosition) const;

    private:
        std::vector<float> compute1DWeights(
            const animator::BlendTreeData& blendTree,
            float paramValue) const;

        std::vector<float> compute2DWeights(
            const animator::BlendTreeData& blendTree,
            float paramX, float paramY) const;

        std::vector<glm::mat4> evaluateAndBlend(
            const animator::BlendTreeData& blendTree,
            const std::vector<float>& weights,
            const resource::SkeletonData& skeleton,
            float stateTime,
            const AnimationLoadCallback& loadCallback,
            bool trackRootMotion,
            glm::vec3& outRootPosition) const;
    };
}
