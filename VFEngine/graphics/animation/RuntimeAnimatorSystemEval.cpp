#include "RuntimeAnimatorSystem.hpp"
#include "AnimationLayerStack.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "threading/JobSystem.hpp"
#include "threading/ParallelView.hpp"
#include "threading/TaskProfiler.hpp"
#include <array>
#include <chrono>

namespace animation
{
    void RuntimeAnimatorSystem::gatherEvalCandidates(
        std::vector<EvalCandidate>& evalCandidates,
        std::vector<std::pair<entt::entity, AnimationLayerStack*>>& lodInterpolateEntities)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        // Phase A (main thread): build a stable-order work list of eligible animators and
        // pre-ensure their persistent LOD-state entries. unordered_map nodes are pointer-stable,
        // so capturing the EntityAnimationLODState* lets the parallel phase do pure lookups with
        // no structural mutation. Pre-creating a default (LOD0) entry for an animator that turns
        // out to be culled this frame is output-identical: the entry is never read for a culled
        // entity (downstream only reads states of active/interpolated entities), and updateEntityLOD
        // never runs on it, so its values stay default exactly as a lazily-created entry would.
        struct WorkItem
        {
            entt::entity entity;
            AnimationLayerStack* anim;
            EntityAnimationLODState* lod;
        };
        std::vector<WorkItem> work;
        work.reserve(animators.size());
        for (auto& [entity, animator] : animators)
        {
            if (!animator || !animator->isInitialized() || !animator->isPlaying())
                continue;
            if (!registry.valid(entity))
                continue;
            work.push_back({entity, animator.get(), &entityLODStates[entity]});
        }

        const uint32_t workCount = static_cast<uint32_t>(work.size());
        if (workCount == 0)
            return;

        // Pre-create the only storage the parallel body touches so concurrent all_of/get are pure
        // reads (EnTT's non-const accessors would otherwise assure<T>() and race the storage map).
        threading::assureStorages<components::WorldTransformComponent>(registry);

        // Per-index output slot: written by exactly one worker, so no contention and the canonical
        // (animators-iteration) order is reproduced by walking perIndex[0..N) in the merge.
        enum class ResultKind : uint8_t { None, Eval, Interp };
        struct SlotResult
        {
            ResultKind kind = ResultKind::None;
            uint8_t lodLevel = 0;
        };
        std::vector<SlotResult> perIndex(workCount);

        // Order-independent accumulators (culled count, LOD histogram) kept thread-local, then summed.
        const uint32_t slotCount = threading::JobSystem::instance().getThreadCount() + 1;
        std::vector<uint32_t> tlCulled(slotCount, 0);
        std::vector<std::array<uint32_t, 4>> tlLodCounts(slotCount, std::array<uint32_t, 4>{});

        auto processRange = [&](uint32_t begin, uint32_t end, uint32_t slot)
        {
            for (uint32_t i = begin; i < end; ++i)
            {
                const WorkItem& item = work[i];
                if (!isEntityInFrustum(item.entity, registry))
                {
                    ++tlCulled[slot];
                    continue;
                }

                EntityAnimationLODState& lodState = *item.lod;
                if (cullingContext.enabled)
                {
                    float distSq = 0.0f;
                    if (registry.all_of<components::WorldTransformComponent>(item.entity))
                    {
                        const auto& wt = registry.get<components::WorldTransformComponent>(item.entity);
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

                ++tlLodCounts[slot][static_cast<uint8_t>(lodState.currentLOD)];

                if (lodManager.shouldEvaluateThisFrame(lodState))
                {
                    perIndex[i] = {ResultKind::Eval, static_cast<uint8_t>(lodState.currentLOD)};
                    lodState.framesSinceLastEval = 0;
                }
                else if (lodState.currentLOD == AnimationLODLevel::LOD1 &&
                         lodState.hasCachedPoseA && lodState.hasCachedPoseB)
                {
                    perIndex[i] = {ResultKind::Interp, 0};
                }
            }
        };

        // Phase B: parallelize only when the work is large enough to amortize fork/join overhead.
        if (workCount < threading::PARALLEL_VIEW_THRESHOLD)
        {
            processRange(0, workCount, 0);
        }
        else
        {
            threading::JobSystem::instance().parallelFor(workCount,
                [&processRange, slotCount](uint32_t begin, uint32_t end, uint32_t threadNum)
                {
                    uint32_t slot = threadNum < slotCount ? threadNum : 0;
                    processRange(begin, end, slot);
                }, 64);
        }

        // Phase C (main thread): merge order-independent counters, then replay outputs in canonical
        // order so leader election in classifyInstanceGroups is byte-identical to the serial path.
        std::array<uint32_t, 4> lodTotals{};
        for (uint32_t s = 0; s < slotCount; ++s)
        {
            culledEntityCount += tlCulled[s];
            for (uint8_t l = 0; l < 4; ++l)
                lodTotals[l] += tlLodCounts[s][l];
        }
        for (uint8_t l = 0; l < 4; ++l)
            lodManager.addLODCount(static_cast<AnimationLODLevel>(l), lodTotals[l]);

        for (uint32_t i = 0; i < workCount; ++i)
        {
            const SlotResult& r = perIndex[i];
            if (r.kind == ResultKind::Eval)
                evalCandidates.push_back({work[i].entity, work[i].anim, r.lodLevel});
            else if (r.kind == ResultKind::Interp)
                lodInterpolateEntities.push_back({work[i].entity, work[i].anim});
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

            // Retargeted entities produce target-skeleton-specific poses, so the same
            // animator path can yield different poses per entity — never instance-share them.
            if (retargetContexts.find(candidate.entity) != retargetContexts.end())
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

        // Measure the gather phase so the Task Graph Profiler shows before/after numbers.
        // Duration-only entry (startTimeNs=0): the per-frame task graph already starts at ~0, so
        // this neither lowers minStart nor exceeds maxEnd in appendToLatestFrame's frame-duration
        // recompute (i.e. it does not perturb viewport FPS).
        auto& profiler = threading::TaskProfiler::instance();
        if (profiler.isEnabled())
        {
            using Clock = std::chrono::high_resolution_clock;
            static const uint32_t gatherThreadId = profiler.getMaxThreadId() + 1;
            auto t0 = Clock::now();
            gatherEvalCandidates(evalCandidates, lodInterpolateEntities);
            auto t1 = Clock::now();

            threading::TaskProfileEntry entry;
            entry.name = "AnimGather";
            entry.threadId = gatherThreadId;
            entry.startTimeNs = 0;
            entry.endTimeNs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            profiler.appendToLatestFrame({entry});
        }
        else
        {
            gatherEvalCandidates(evalCandidates, lodInterpolateEntities);
        }

        ActiveAnimatorList leadersToEvaluate;
        std::vector<std::pair<entt::entity, AnimationLayerStack*>> followersToSync;

        classifyInstanceGroups(evalCandidates, leadersToEvaluate, followersToSync);
        evaluateLeadersAndSync(leadersToEvaluate, followersToSync, activeAnimators, deltaTime);
        cachePosesAndInterpolate(activeAnimators, lodInterpolateEntities);

        return activeAnimators;
    }
}
