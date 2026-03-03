#include "BlendTree.hpp"
#include <algorithm>
#include <cmath>

namespace animation
{
    std::vector<glm::mat4> BlendTreeEvaluator::evaluate(
        const animator::BlendTreeData& blendTree,
        const animator::AnimatorRuntimeParameters& params,
        const resource::SkeletonData& skeleton,
        float stateTime,
        const AnimationLoadCallback& loadCallback) const
    {
        glm::vec3 unused;
        return evaluateAndBlend(blendTree, {}, skeleton, stateTime, loadCallback, false, unused);
    }

    std::vector<glm::mat4> BlendTreeEvaluator::evaluate(
        const animator::BlendTreeData& blendTree,
        const animator::AnimatorRuntimeParameters& params,
        const resource::SkeletonData& skeleton,
        float stateTime,
        const AnimationLoadCallback& loadCallback,
        glm::vec3& outRootPosition) const
    {
        std::vector<float> weights;

        if (blendTree.type == animator::BlendTreeType::BlendTree1D)
        {
            float paramValue = params.getFloat(blendTree.parameterName);
            weights = compute1DWeights(blendTree, paramValue);
        }
        else
        {
            float paramX = params.getFloat(blendTree.parameterName);
            float paramY = params.getFloat(blendTree.parameterNameY);
            weights = compute2DWeights(blendTree, paramX, paramY);
        }

        return evaluateAndBlend(blendTree, weights, skeleton, stateTime, loadCallback, true, outRootPosition);
    }

    std::vector<float> BlendTreeEvaluator::compute1DWeights(
        const animator::BlendTreeData& blendTree,
        float paramValue) const
    {
        const auto& entries = blendTree.entries;
        size_t n = entries.size();
        std::vector<float> weights(n, 0.0f);

        if (n == 0)
            return weights;
        if (n == 1)
        {
            weights[0] = 1.0f;
            return weights;
        }

        // Build sorted index by threshold
        std::vector<size_t> sorted(n);
        for (size_t i = 0; i < n; ++i)
            sorted[i] = i;
        std::sort(sorted.begin(), sorted.end(), [&entries](size_t a, size_t b) {
            return entries[a].threshold < entries[b].threshold;
        });

        // Clamp to range
        float minT = entries[sorted.front()].threshold;
        float maxT = entries[sorted.back()].threshold;
        paramValue = std::clamp(paramValue, minT, maxT);

        // Find neighboring entries
        if (paramValue <= entries[sorted[0]].threshold)
        {
            weights[sorted[0]] = 1.0f;
            return weights;
        }
        if (paramValue >= entries[sorted[n - 1]].threshold)
        {
            weights[sorted[n - 1]] = 1.0f;
            return weights;
        }

        for (size_t i = 0; i < n - 1; ++i)
        {
            size_t lo = sorted[i];
            size_t hi = sorted[i + 1];

            if (paramValue >= entries[lo].threshold && paramValue <= entries[hi].threshold)
            {
                float range = entries[hi].threshold - entries[lo].threshold;
                if (range < 0.0001f)
                {
                    weights[lo] = 0.5f;
                    weights[hi] = 0.5f;
                }
                else
                {
                    float t = (paramValue - entries[lo].threshold) / range;
                    weights[lo] = 1.0f - t;
                    weights[hi] = t;
                }
                return weights;
            }
        }

        weights[sorted[0]] = 1.0f;
        return weights;
    }

    std::vector<float> BlendTreeEvaluator::compute2DWeights(
        const animator::BlendTreeData& blendTree,
        float paramX, float paramY) const
    {
        const auto& entries = blendTree.entries;
        size_t n = entries.size();
        std::vector<float> weights(n, 0.0f);

        if (n == 0)
            return weights;
        if (n == 1)
        {
            weights[0] = 1.0f;
            return weights;
        }

        // Gradient band interpolation (simplified Cartesian weights)
        glm::vec2 point(paramX, paramY);

        float totalWeight = 0.0f;
        for (size_t i = 0; i < n; ++i)
        {
            float minInfluence = std::numeric_limits<float>::max();

            for (size_t j = 0; j < n; ++j)
            {
                if (i == j)
                    continue;

                glm::vec2 pi = entries[i].position;
                glm::vec2 pj = entries[j].position;
                glm::vec2 diff = pj - pi;
                float dSq = glm::dot(diff, diff);

                if (dSq < 0.0001f)
                    continue;

                float influence = 1.0f - glm::dot(point - pi, diff) / dSq;
                influence = std::clamp(influence, 0.0f, 1.0f);
                minInfluence = std::min(minInfluence, influence);
            }

            if (minInfluence == std::numeric_limits<float>::max())
                minInfluence = 1.0f;

            weights[i] = minInfluence;
            totalWeight += minInfluence;
        }

        // Normalize
        if (totalWeight > 0.0001f)
        {
            for (float& w : weights)
                w /= totalWeight;
        }

        return weights;
    }

    std::vector<glm::mat4> BlendTreeEvaluator::evaluateAndBlend(
        const animator::BlendTreeData& blendTree,
        const std::vector<float>& weightsIn,
        const resource::SkeletonData& skeleton,
        float stateTime,
        const AnimationLoadCallback& loadCallback,
        bool trackRootMotion,
        glm::vec3& outRootPosition) const
    {
        const auto& entries = blendTree.entries;
        size_t n = entries.size();

        if (n == 0)
            return {};

        // Use provided weights or default to uniform
        std::vector<float> weights = weightsIn;
        if (weights.empty())
        {
            weights.resize(n, 1.0f / static_cast<float>(n));
        }

        // Evaluate each active entry's pose
        std::vector<std::vector<glm::mat4>> poses;
        std::vector<float> activeWeights;
        std::vector<glm::vec3> rootPositions;

        AnimationEvaluator evaluator;

        for (size_t i = 0; i < n && i < weights.size(); ++i)
        {
            if (weights[i] <= 0.001f || entries[i].animationPath.empty())
                continue;

            const resource::AnimationData* animData = loadCallback(entries[i].animationPath);
            if (!animData)
                continue;

            evaluator.loadAnimation(*animData, skeleton);
            float timeInTicks = evaluator.secondsToTicks(stateTime);

            if (trackRootMotion)
            {
                glm::vec3 rootPos;
                poses.push_back(evaluator.evaluatePose(timeInTicks, rootPos));
                rootPositions.push_back(rootPos);
            }
            else
            {
                poses.push_back(evaluator.evaluatePose(timeInTicks));
            }
            activeWeights.push_back(weights[i]);
        }

        if (poses.empty())
            return {};

        if (poses.size() == 1)
        {
            if (trackRootMotion && !rootPositions.empty())
                outRootPosition = rootPositions[0];
            return poses[0];
        }

        // Normalize active weights
        float totalW = 0.0f;
        for (float w : activeWeights)
            totalW += w;
        if (totalW > 0.0001f)
        {
            for (float& w : activeWeights)
                w /= totalW;
        }

        // Blend root positions
        if (trackRootMotion)
        {
            outRootPosition = glm::vec3(0.0f);
            for (size_t i = 0; i < rootPositions.size(); ++i)
                outRootPosition += rootPositions[i] * activeWeights[i];
        }

        return AnimationBlender::blendNPoses(poses, activeWeights);
    }
}
