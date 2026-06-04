#pragma once

#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <optional>

namespace events::animation::snapshot
{
    struct AnimationLayerSnapshot
    {
        // StateMachine state
        uint32_t currentStateId = 0;
        uint32_t previousStateId = 0;
        float stateTime = 0.0f;
        float previousStateTime = 0.0f;
        float blendWeight = 0.0f;
        float blendDuration = 0.0f;
        float blendElapsed = 0.0f;
        bool isBlending = false;
        bool isPlaying = true;
        uint32_t currentLoopCount = 0;
        float previousNormalizedTime = 0.0f;

        // DirectClip state
        float clipTime = 0.0f;
        bool clipPlaying = true;

        // Layer weight
        float weight = 1.0f;
    };

    struct AnimationSnapshot
    {
        std::vector<AnimationLayerSnapshot> layers;
        std::unordered_map<std::string, std::variant<float, int32_t, bool>> parameters;
        bool rootMotionEnabled = false;
        std::vector<glm::mat4> frozenPose; // Only populated for LOD3 entities
    };

    struct CaptureAnimationSnapshotQuery : ::events::IQuery<std::optional<AnimationSnapshot>>
    {
        services::EntityHandle entity;

        std::string_view getName() const override { return "CaptureAnimationSnapshot"; }
    };

    struct RestoreAnimationSnapshotCommand : ::events::ICommand<void>
    {
        services::EntityHandle entity;
        AnimationSnapshot snapshot;

        std::string_view getName() const override { return "RestoreAnimationSnapshot"; }
    };

} // namespace events::animation::snapshot
