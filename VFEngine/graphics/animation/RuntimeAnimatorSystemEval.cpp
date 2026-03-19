#include "RuntimeAnimatorSystem.hpp"
#include "AnimationLayerStack.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "threading/JobSystem.hpp"

namespace animation
{
    void RuntimeAnimatorSystem::gatherEvalCandidates(
        std::vector<EvalCandidate>& evalCandidates,
        std::vector<std::pair<entt::entity, AnimationLayerStack*>>& lodInterpolateEntities)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        for (auto& [entity, animator] : animators)
        {
            if (!animator || !animator->isInitialized() || !animator->isPlaying())
                continue;
            if (!registry.valid(entity))
                continue;

            if (!isEntityInFrustum(entity, registry))
            {
                ++culledEntityCount;
                continue;
            }

            auto& lodState = entityLODStates[entity];
            if (cullingContext.enabled)
            {
                float distSq = 0.0f;
                if (registry.all_of<components::WorldTransformComponent>(entity))
                {
                    const auto& wt = registry.get<components::WorldTransformComponent>(entity);
                    glm::vec3 pos = glm::vec3(wt.worldMatrix[3]);
                    glm::vec3 diff = pos - cullingContext.cameraPos;
                    distSq = glm::dot(diff, diff);
                }
                lodManager.updateEntityLOD(lodState, distSq);
            }
            else
            {
                lodManager.updateEntityLOD(lodState, 0.0f);
            }

            lodManager.incrementLODCount(lodState.currentLOD);

            if (lodManager.shouldEvaluateThisFrame(lodState))
            {
                uint8_t lodLevel = static_cast<uint8_t>(lodState.currentLOD);
                evalCandidates.push_back({entity, animator.get(), lodLevel});
                lodState.framesSinceLastEval = 0;
            }
            else if (lodState.currentLOD == AnimationLODLevel::LOD1 &&
                     lodState.hasCachedPoseA && lodState.hasCachedPoseB)
            {
                lodInterpolateEntities.push_back({entity, animator.get()});
            }
        }
    }

    void RuntimeAnimatorSystem::classifyInstanceGroups(
        std::vector<EvalCandidate>& evalCandidates,
        ActiveAnimatorList& leadersToEvaluate,
        std::vector<std::pair<entt::entity, AnimationLayerStack*>>& followersToSync)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        for (auto& candidate : evalCandidates)
        {
            if (candidate.anim->isBlending())
            {
                leadersToEvaluate.push_back({candidate.entity, candidate.anim});
                continue;
            }

            const AnimatorStateMachine* baseSM = candidate.anim->getBaseStateMachine();
            const animator::AnimatorState* currentState = baseSM ? baseSM->getCurrentAnimatorState() : nullptr;
            if (!currentState)
            {
                leadersToEvaluate.push_back({candidate.entity, candidate.anim});
                continue;
            }

            std::string animatorPath;
            if (registry.all_of<components::AnimatorComponent>(candidate.entity))
                animatorPath = registry.get<components::AnimatorComponent>(candidate.entity).animatorRef.resolve();

            if (animatorPath.empty())
            {
                leadersToEvaluate.push_back({candidate.entity, candidate.anim});
                continue;
            }

            float normalizedTime = candidate.anim->getNormalizedTime();
            uint64_t groupKey = computeInstanceGroupKey(animatorPath, currentState->id,
                                                         candidate.lodLevel, normalizedTime);
            auto& group = instanceGroups[groupKey];
            if (group.leader == entt::null)
            {
                group.leader = candidate.entity;
                leadersToEvaluate.push_back({candidate.entity, candidate.anim});
            }
            else
            {
                group.followers.push_back(candidate.entity);
                followersToSync.push_back({candidate.entity, candidate.anim});
            }
        }

        for (const auto& [key, group] : instanceGroups)
            if (!group.followers.empty())
                ++activeInstanceGroupCount;
    }

    void RuntimeAnimatorSystem::evaluateLeadersAndSync(
        ActiveAnimatorList& leadersToEvaluate,
        std::vector<std::pair<entt::entity, AnimationLayerStack*>>& followersToSync,
        ActiveAnimatorList& activeAnimators, float deltaTime)
    {
        if (leadersToEvaluate.size() > 1)
        {
            uint32_t leaderCount = static_cast<uint32_t>(leadersToEvaluate.size());
            threading::JobSystem::instance().parallelFor(leaderCount,
                [&leadersToEvaluate, deltaTime](uint32_t begin, uint32_t end)
                {
                    for (uint32_t i = begin; i < end; ++i)
                    {
                        leadersToEvaluate[i].second->update(deltaTime);
                    }
                }, 1);
        }
        else if (!leadersToEvaluate.empty())
        {
            leadersToEvaluate[0].second->update(deltaTime);
        }

        for (const auto& [key, group] : instanceGroups)
        {
            if (group.followers.empty())
                continue;

            auto leaderIt = animators.find(group.leader);
            if (leaderIt == animators.end())
                continue;

            const auto& leaderMatrices = leaderIt->second->getBoneMatrices();
            for (entt::entity follower : group.followers)
            {
                auto followerIt = animators.find(follower);
                if (followerIt != animators.end())
                {
                    followerIt->second->getMutableBoneMatrices() = leaderMatrices;
                }
            }
        }

        activeAnimators = std::move(leadersToEvaluate);
        for (auto& [entity, anim] : followersToSync)
        {
            activeAnimators.push_back({entity, anim});
        }
    }

    void RuntimeAnimatorSystem::cachePosesAndInterpolate(
        ActiveAnimatorList& activeAnimators,
        std::vector<std::pair<entt::entity, AnimationLayerStack*>>& lodInterpolateEntities)
    {
        for (auto& [entity, anim] : activeAnimators)
        {
            auto it = entityLODStates.find(entity);
            if (it != entityLODStates.end() &&
                it->second.currentLOD == AnimationLODLevel::LOD1)
            {
                lodManager.cachePose(it->second, anim->getBoneMatrices());
            }
        }

        for (auto& [entity, anim] : lodInterpolateEntities)
        {
            auto it = entityLODStates.find(entity);
            if (it != entityLODStates.end())
            {
                uint8_t lodIdx = static_cast<uint8_t>(it->second.currentLOD);
                uint32_t interval = lodManager.getConfig().updateIntervals[lodIdx];
                float t = interval > 0
                    ? static_cast<float>(it->second.framesSinceLastEval) / static_cast<float>(interval)
                    : 0.5f;
                std::vector<glm::mat4> interpolated;
                lodManager.interpolateCachedPoses(it->second, t, interpolated);
                if (!interpolated.empty())
                {
                    anim->getMutableBoneMatrices() = std::move(interpolated);
                    activeAnimators.push_back({entity, anim});
                }
            }
        }
    }

    RuntimeAnimatorSystem::ActiveAnimatorList RuntimeAnimatorSystem::evaluateAnimations(float deltaTime)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        culledEntityCount = 0;
        lodManager.resetLODCounts();
        instanceGroups.clear();
        activeInstanceGroupCount = 0;

        ActiveAnimatorList activeAnimators;
        std::vector<std::pair<entt::entity, AnimationLayerStack*>> lodInterpolateEntities;
        std::vector<EvalCandidate> evalCandidates;

        gatherEvalCandidates(evalCandidates, lodInterpolateEntities);

        ActiveAnimatorList leadersToEvaluate;
        std::vector<std::pair<entt::entity, AnimationLayerStack*>> followersToSync;

        classifyInstanceGroups(evalCandidates, leadersToEvaluate, followersToSync);
        evaluateLeadersAndSync(leadersToEvaluate, followersToSync, activeAnimators, deltaTime);
        cachePosesAndInterpolate(activeAnimators, lodInterpolateEntities);

        return activeAnimators;
    }
}
