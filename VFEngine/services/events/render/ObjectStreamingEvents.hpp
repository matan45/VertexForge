#pragma once
#include "../EventTypes.hpp"
#include "../../../graphics/render/gpudriven/scene/GPUObjectStreamTypes.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <entt/entt.hpp>

// VK-1594: only referenced through a const pointer below, so the definition is not needed here.
namespace world { struct HLODFileData; }

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

    // VK-1594: a baked HLOD proxy's geometry lives only in RAM - there is no .vfMesh for the
    // renderer to stream - so it is pushed straight into MergedMeshBuffer under a synthetic key
    // (the .vfHLOD path). The proxy entity then carries a normal MeshComponent pointing at that
    // key and draws through the ordinary GPU-driven path.
    //
    // `data` is a BORROWED pointer, valid only for the duration of this synchronous execute().
    // Nothing downstream retains it: the adapter builds non-owning views for the upload and the
    // bytes are copied to the GPU before the call returns.
    struct RegisterHLODMeshCommand : ::events::ICommand<bool>
    {
        std::string meshKey;
        const ::world::HLODFileData* data = nullptr;
        std::string_view getName() const override { return "RegisterHLODMesh"; }
    };

    struct ReleaseHLODMeshCommand : ::events::ICommand<void>
    {
        std::string meshKey;
        std::string_view getName() const override { return "ReleaseHLODMesh"; }
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
