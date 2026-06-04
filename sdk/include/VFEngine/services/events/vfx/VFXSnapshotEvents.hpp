#pragma once

#include "../EventTypes.hpp"
#include "../../data/VFXTypes.hpp"
#include <optional>

namespace events::vfx::snapshot
{
    struct VFXPlaybackSnapshot
    {
        float emissionTime = 0.0f;
        float spawnAccumulator = 0.0f;
        bool wasPlaying = true;
        bool wasActive = true;
    };

    struct CaptureVFXSnapshotQuery : ::events::IQuery<std::optional<VFXPlaybackSnapshot>>
    {
        services::VFXInstanceId instanceId = 0;
        std::string_view getName() const override { return "CaptureVFXSnapshot"; }
    };

    struct SeekVFXInstanceCommand : ::events::ICommand<void>
    {
        services::VFXInstanceId instanceId = 0;
        float emissionTime = 0.0f;
        float spawnAccumulator = 0.0f;
        std::string_view getName() const override { return "SeekVFXInstance"; }
    };

    struct GetVFXSnapshotQuery : ::events::IQuery<std::optional<VFXPlaybackSnapshot>>
    {
        uint64_t entityUUID = 0;
        std::string_view getName() const override { return "GetVFXSnapshot"; }
    };

} // namespace events::vfx::snapshot
