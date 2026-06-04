#pragma once
#include "../EventTypes.hpp"
#include "../../../graphics/render/gpudriven/scene/GPUObjectStreamTypes.hpp"
#include <cstdint>
#include <vector>
#include <utility>
#include <entt/entt.hpp>

namespace events::render::objectstreaming
{
    // ---- Commands ----

    struct SetObjectStreamingEnabledCommand : ::events::ICommand<void>
    {
        bool enabled = false;
        std::string_view getName() const override { return "SetObjectStreamingEnabled"; }
    };

    struct SetObjectStreamingConfigCommand : ::events::ICommand<void>
    {
        ::render::gpudriven::ObjectStreamConfig config;
        std::string_view getName() const override { return "SetObjectStreamingConfig"; }
    };

    struct RegisterSectorObjectsCommand : ::events::ICommand<void>
    {
        uint32_t sectorId;
        std::vector<std::pair<uint64_t, entt::entity>> entities;
        std::string_view getName() const override { return "RegisterSectorObjects"; }
    };

    struct UnregisterSectorObjectsCommand : ::events::ICommand<void>
    {
        uint32_t sectorId;
        std::string_view getName() const override { return "UnregisterSectorObjects"; }
    };

    // ---- Queries ----

    struct GetObjectStreamingStatsQuery : ::events::IQuery<::render::gpudriven::ObjectStreamingStats>
    {
        std::string_view getName() const override { return "GetObjectStreamingStats"; }
    };

    struct GetObjectStreamingConfigQuery : ::events::IQuery<::render::gpudriven::ObjectStreamConfig>
    {
        std::string_view getName() const override { return "GetObjectStreamingConfig"; }
    };
}
