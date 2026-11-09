#pragma once
#include "../EventTypes.hpp"
#include "../../data/HeightmapGenerationData.hpp"
#include <cstdint>

// VK-1653. Procedural heightmap generation for MCP terrain_generate_heightmap.
//
// Handled by editor/handlers/HeightmapGenerationHandler: ProceduralGen.dll is linked by the Editor
// only, and EventDispatcher is per-binary, so the handler has to live in Editor.exe (the same shape
// as game_export / ExportHandler).
//
// Generation is single-threaded noise over up to 4096^2 pixels -- seconds, not milliseconds -- so it
// runs on a background thread: Begin starts it, Poll reports progress. Poll is a COMMAND, not a
// query, because the completing poll publishes AssetSavedNotification on the main thread. Both are
// main-thread only.

namespace events::heightmapGeneration
{
    // Refused (accepted = false) while another job is running, or for an invalid request.
    struct BeginHeightmapGenerationCommand : ICommand<services::HeightmapJobStart>
    {
        services::HeightmapGenerationRequest request;

        std::string_view getName() const override { return "BeginHeightmapGeneration"; }
    };

    struct PollHeightmapGenerationCommand : ICommand<services::HeightmapJobStatus>
    {
        uint64_t jobId = 0;

        std::string_view getName() const override { return "PollHeightmapGeneration"; }
    };
}
