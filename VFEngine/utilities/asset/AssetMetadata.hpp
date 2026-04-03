#pragma once
#include "AssetGUID.hpp"
#include "../resource/AssetTypes.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <tuple>
#include <optional>

namespace asset
{
    struct FragmentPhysicsInfo
    {
        glm::vec3 centerOfMass{0.0f};
        float volume = 0.0f;
        glm::vec3 bboxMin{0.0f};
        glm::vec3 bboxMax{0.0f};
    };

    struct FractureMetadata
    {
        uint32_t fragmentCount = 0;
        uint32_t seedDistribution = 0;
        uint32_t randomSeed = 42;
        float innerUVScale = 1.0f;
        std::vector<FragmentPhysicsInfo> fragments;
        std::vector<std::tuple<uint32_t, uint32_t, float>> connectivity;
    };

    struct AssetMetadata
    {
        AssetGUID guid;
        resource::AssetType type = resource::AssetType::COUNT;
        std::string importSourcePath;
        std::string importTimestamp;
        uint32_t formatVersion = 1;
        std::optional<FractureMetadata> fractureData;
    };
}
