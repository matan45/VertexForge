#pragma once

#include "resource/Types.hpp"
#include <bitset>
#include <cstdint>
#include <string>
#include <vector>

namespace animation
{
    // Maximum bones supported per skeleton for LOD bitset
    constexpr uint32_t MAX_SKELETON_BONES = 256;

    // Bitset marking which bones are active at a given LOD level
    using BoneLODSet = std::bitset<MAX_SKELETON_BONES>;

    struct BoneLODSets
    {
        BoneLODSet essential;  // Core bones (spine, arms, legs, head)
        BoneLODSet full;       // All bones

        // LOD 0-1: full set, LOD 2: essential, LOD 3: none (frozen)
        const BoneLODSet& getSetForLOD(uint8_t lodLevel) const
        {
            if (lodLevel >= 2)
                return essential;
            return full;
        }
    };

    // Auto-generate bone LOD sets from skeleton hierarchy
    // Detail bones: depth > maxEssentialDepth from root, or name contains
    // finger/toe/face/eye/jaw keywords
    inline BoneLODSets generateBoneLODSets(const resource::SkeletonData& skeleton,
                                            int maxEssentialDepth = 4)
    {
        BoneLODSets sets;
        const size_t boneCount = skeleton.bones.size();
        if (boneCount == 0 || boneCount > MAX_SKELETON_BONES)
        {
            // Fallback: all bones essential
            sets.full.set();
            sets.essential.set();
            return sets;
        }

        // Compute depth for each bone in O(n) using topological order
        // (Assimp guarantees parents come before children)
        std::vector<int> depths(boneCount, 0);
        for (size_t i = 0; i < boneCount; ++i)
        {
            int parent = skeleton.bones[i].parentIndex;
            depths[i] = (parent >= 0 && parent < static_cast<int>(boneCount))
                ? depths[parent] + 1
                : 0;
        }

        // Detail bone name keywords (case-insensitive check via lowercase)
        auto containsKeyword = [](const std::string& name) -> bool
        {
            std::string lower = name;
            for (auto& c : lower) c = static_cast<char>(std::tolower(c));
            return lower.find("finger") != std::string::npos ||
                   lower.find("toe") != std::string::npos ||
                   lower.find("face") != std::string::npos ||
                   lower.find("eye") != std::string::npos ||
                   lower.find("jaw") != std::string::npos ||
                   lower.find("tongue") != std::string::npos ||
                   lower.find("brow") != std::string::npos ||
                   lower.find("lip") != std::string::npos ||
                   lower.find("cheek") != std::string::npos ||
                   lower.find("nostril") != std::string::npos;
        };

        for (size_t i = 0; i < boneCount; ++i)
        {
            sets.full.set(i);

            bool isDetail = depths[i] > maxEssentialDepth || containsKeyword(skeleton.bones[i].name);
            if (!isDetail)
            {
                sets.essential.set(i);
            }
        }

        return sets;
    }
}
