#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>

namespace physics
{
    // One in-flight hit reaction: the affected chain's motor strength dips to
    // dipStrength on impact and recovers to full over recoverTime (smoothstep).
    struct HitReactionInstance
    {
        std::vector<int> affectedPhysicsBones;
        float elapsed = 0.0f;
        float recoverTime = 0.6f;
        float dipStrength = 0.0f;
    };

    struct HitReactionState
    {
        std::vector<HitReactionInstance> active;

        void update(float deltaTime)
        {
            for (auto& instance : active)
                instance.elapsed += deltaTime;
            active.erase(std::remove_if(active.begin(), active.end(),
                                        [](const HitReactionInstance& instance)
                                        { return instance.elapsed >= instance.recoverTime; }),
                         active.end());
        }

        // Multiplier on the bone's motor strength. Overlapping reactions take the minimum.
        float scaleForBone(int physicsBoneIndex) const
        {
            float scale = 1.0f;
            for (const auto& instance : active)
            {
                bool affected = false;
                for (int bone : instance.affectedPhysicsBones)
                {
                    if (bone == physicsBoneIndex)
                    {
                        affected = true;
                        break;
                    }
                }
                if (!affected) continue;

                float t = instance.recoverTime > 0.0f
                              ? std::clamp(instance.elapsed / instance.recoverTime, 0.0f, 1.0f)
                              : 1.0f;
                float smooth = t * t * (3.0f - 2.0f * t);
                scale = std::min(scale, instance.dipStrength + (1.0f - instance.dipStrength) * smooth);
            }
            return scale;
        }

        // Hit bone plus its descendants in the physics skeleton, walked via parent indices.
        // maxDepth < 0 means unlimited; maxDepth == 0 means only the hit bone itself.
        static std::vector<int> resolveChain(int hitBone, const std::vector<int>& parentIndices, int maxDepth)
        {
            std::vector<int> chain;
            if (hitBone < 0 || hitBone >= static_cast<int>(parentIndices.size()))
                return chain;

            std::vector<int> depth(parentIndices.size(), -1);
            depth[hitBone] = 0;
            chain.push_back(hitBone);

            // Parents always precede children in skeleton order, so one forward pass suffices
            for (int bone = hitBone + 1; bone < static_cast<int>(parentIndices.size()); ++bone)
            {
                int parent = parentIndices[bone];
                if (parent < 0 || parent >= bone || depth[parent] < 0) continue;
                int boneDepth = depth[parent] + 1;
                if (maxDepth >= 0 && boneDepth > maxDepth) continue;
                depth[bone] = boneDepth;
                chain.push_back(bone);
            }
            return chain;
        }
    };

    // Combines the static per-bone profile with the hit-reaction dip and the
    // script-controlled global multiplier; result clamped to [0, 1].
    inline std::vector<float> resolveEffectiveStrengths(const std::vector<float>& profileStrengths,
                                                        const HitReactionState& hitReactions,
                                                        float globalStrength)
    {
        std::vector<float> effective(profileStrengths.size());
        for (size_t i = 0; i < profileStrengths.size(); ++i)
        {
            float s = profileStrengths[i] * hitReactions.scaleForBone(static_cast<int>(i)) * globalStrength;
            effective[i] = std::clamp(s, 0.0f, 1.0f);
        }
        return effective;
    }
}
