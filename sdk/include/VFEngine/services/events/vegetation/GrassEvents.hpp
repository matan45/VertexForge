#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "vegetation/GrassConfig.hpp"
#include "vegetation/VegetationTypes.hpp"
#include "vegetation/VegetationScatterTypes.hpp"
#include <optional>
#include <vector>

namespace events::vegetation
{
    struct SetGrassConfigCommand : ICommand<void>
    {
        services::EntityHandle entityId;
        ::vegetation::GrassRenderConfig config;

        std::string_view getName() const override { return "SetGrassConfig"; }
    };

    struct GetGrassConfigQuery : IQuery<::vegetation::GrassRenderConfig>
    {
        services::EntityHandle entityId;

        std::string_view getName() const override { return "GetGrassConfig"; }
    };

    struct GrassConfigChangedNotification : INotification
    {
        services::EntityHandle entityId;

        std::string_view getName() const override { return "GrassConfigChanged"; }
    };

    // Global versions — operate on first entity with GrassComponent (no entity handle needed)
    struct SetGlobalGrassConfigCommand : ICommand<void>
    {
        ::vegetation::GrassRenderConfig config;

        std::string_view getName() const override { return "SetGlobalGrassConfig"; }
    };

    struct GetGlobalGrassConfigQuery : IQuery<::vegetation::GrassRenderConfig>
    {
        std::string_view getName() const override { return "GetGlobalGrassConfig"; }
    };

    // Scatter profile (VK-1581) on the first GrassComponent — persisted in scene JSON.
    struct SetGlobalScatterProfileCommand : ICommand<void>
    {
        ::vegetation::ScatterProfile profile;

        std::string_view getName() const override { return "SetGlobalScatterProfile"; }
    };

    struct GetGlobalScatterProfileQuery : IQuery<::vegetation::ScatterProfile>
    {
        std::string_view getName() const override { return "GetGlobalScatterProfile"; }
    };

    struct SetBillboardPaletteCommand : ICommand<void>
    {
        std::vector<::vegetation::BillboardPaletteEntry> entries;
        int32_t activeEntry = -1;

        std::string_view getName() const override { return "SetBillboardPalette"; }
    };

    struct GetBillboardPaletteQuery : IQuery<std::vector<::vegetation::BillboardPaletteEntry>>
    {
        std::string_view getName() const override { return "GetBillboardPalette"; }
    };

    struct ClearAllBillboardInstancesCommand : ICommand<void>
    {
        std::string_view getName() const override { return "ClearAllBillboardInstances"; }
    };

    // Add billboard instances to a specific tile
    struct AddBillboardInstancesToTileCommand : ICommand<void>
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;
        std::vector<::vegetation::BillboardInstance> instances;

        std::string_view getName() const override { return "AddBillboardInstancesToTile"; }
    };

    // Get billboard instances for a tile (for spatial grid rebuild)
    struct GetTileBillboardInstancesQuery : IQuery<std::vector<::vegetation::BillboardInstance>>
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;

        std::string_view getName() const override { return "GetTileBillboardInstances"; }
    };

    // Remove billboard instances from a specific tile by indices (sorted descending)
    struct RemoveBillboardInstancesFromTileCommand : ICommand<void>
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;
        std::vector<uint32_t> indicesToRemove; // Must be sorted descending

        std::string_view getName() const override { return "RemoveBillboardInstancesFromTile"; }
    };

    // Replace a tile's entire billboard instance set (used by undo/redo snapshots)
    struct SetTileBillboardInstancesCommand : ICommand<void>
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;
        std::vector<::vegetation::BillboardInstance> instances;

        std::string_view getName() const override { return "SetTileBillboardInstances"; }
    };

    // A world-space XZ region for a scatter bake (VK-1581). On the command, nullopt = whole terrain.
    struct ScatterRegion
    {
        float minX = 0.0f;
        float minZ = 0.0f;
        float maxX = 0.0f;
        float maxZ = 0.0f;
    };

    // Bake procedural vegetation billboards into terrain tiles from a scatter profile as
    // ONE undoable stroke. replaceProcedural (Regenerate) clears existing procedural-source
    // instances in the region first; hand-painted instances are always preserved.
    struct GenerateVegetationScatterCommand : ICommand<void>
    {
        std::optional<ScatterRegion> region;   // nullopt = whole active terrain
        ::vegetation::ScatterProfile profile;
        uint32_t seed = 1337;
        bool replaceProcedural = true;

        std::string_view getName() const override { return "GenerateVegetationScatter"; }
    };

    // Broadcast when a scatter bake completes — drives the panel's placed-count / budget warning.
    struct ScatterBakeCompletedNotification : INotification
    {
        uint32_t placedCount = 0;    // procedural instances placed this bake
        uint32_t totalCount = 0;     // total billboards after the bake (painted + procedural)
        bool budgetExceeded = false; // hit the GPU instance budget

        std::string_view getName() const override { return "ScatterBakeCompleted"; }
    };
}
