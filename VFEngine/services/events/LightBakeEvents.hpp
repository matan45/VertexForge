#pragma once
#include "EventTypes.hpp"
#include "../providers/ILightBakeProvider.hpp"
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace services::events::lightbake
{
    // ============================================================
    // COMMANDS
    // ============================================================

    struct StartBakeCommand : ::events::ICommand<void>
    {
        services::LightBakeConfig config;
        std::string_view getName() const override { return "StartBake"; }
    };

    struct CancelBakeCommand : ::events::ICommand<void>
    {
        std::string_view getName() const override { return "CancelBake"; }
    };

    struct LoadLightmapCommand : ::events::ICommand<bool>
    {
        std::string lightmapPath;
        float texelsPerUnit = 16.0f;
        std::string_view getName() const override { return "LoadLightmap"; }
    };

    struct ClearLightmapCommand : ::events::ICommand<void>
    {
        std::string_view getName() const override { return "ClearLightmap"; }
    };

    // ============================================================
    // QUERIES
    // ============================================================

    struct GetBakeProgressQuery : ::events::IQuery<float>
    {
        std::string_view getName() const override { return "GetBakeProgress"; }
    };

    struct IsBakingQuery : ::events::IQuery<bool>
    {
        std::string_view getName() const override { return "IsBaking"; }
    };

    struct GetBakeResultQuery : ::events::IQuery<services::LightBakeResult>
    {
        std::string_view getName() const override { return "GetBakeResult"; }
    };

    // Terrain tile lightmap info from last bake
    struct TerrainTileLightmapEntry
    {
        int32_t coordX = 0;
        int32_t coordZ = 0;
        glm::vec4 scaleOffset{1.0f, 1.0f, 0.0f, 0.0f};
        std::string lightmapPath;
    };

    struct GetTerrainLightmapDataQuery : ::events::IQuery<std::vector<TerrainTileLightmapEntry>>
    {
        std::string_view getName() const override { return "GetTerrainLightmapData"; }
    };

    // ============================================================
    // NOTIFICATIONS
    // ============================================================

    struct BakeCompletedNotification : ::events::INotification
    {
        services::LightBakeResult result;
        std::string_view getName() const override { return "BakeCompleted"; }
    };

    struct BakeFailedNotification : ::events::INotification
    {
        std::string errorMessage;
        std::string_view getName() const override { return "BakeFailed"; }
    };

    struct LightmapLoadedNotification : ::events::INotification
    {
        std::string lightmapPath;
        std::string_view getName() const override { return "LightmapLoaded"; }
    };

    struct LightmapClearedNotification : ::events::INotification
    {
        std::string_view getName() const override { return "LightmapCleared"; }
    };
}
