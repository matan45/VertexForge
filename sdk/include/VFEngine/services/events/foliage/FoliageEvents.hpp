#pragma once
// VK-1575: CQRS events for authoring the per-tile foliage instance store + FoliageType
// palette (both owned by TerrainService). Mirrors the vegetation tile block in GrassEvents.hpp.
#include "../EventTypes.hpp"
#include "foliage/FoliageTypes.hpp"
#include "vegetation/VegetationScatterTypes.hpp"
#include "../vegetation/GrassEvents.hpp" // events::vegetation::ScatterRegion (VK-1585)
#include <optional>
#include <vector>
#include <cstdint>

namespace events::foliage
{
    // Palette lives on TerrainService; the render collector reads it via getFoliagePalette().
    struct SetFoliagePaletteCommand : ICommand<void>
    {
        std::vector<::foliage::FoliageType> palette;

        std::string_view getName() const override { return "SetFoliagePalette"; }
    };

    struct GetFoliagePaletteQuery : IQuery<std::vector<::foliage::FoliageType>>
    {
        std::string_view getName() const override { return "GetFoliagePalette"; }
    };

    struct ClearAllFoliageInstancesCommand : ICommand<void>
    {
        std::string_view getName() const override { return "ClearAllFoliageInstances"; }
    };

    // Add foliage instances to a specific tile.
    struct AddFoliageInstancesToTileCommand : ICommand<void>
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;
        std::vector<::foliage::FoliageInstance> instances;

        std::string_view getName() const override { return "AddFoliageInstancesToTile"; }
    };

    // Get a tile's foliage instances (spatial-grid rebuild + undo "after" snapshot).
    struct GetTileFoliageInstancesQuery : IQuery<std::vector<::foliage::FoliageInstance>>
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;

        std::string_view getName() const override { return "GetTileFoliageInstances"; }
    };

    // Remove foliage instances from a tile by index (indices MUST be sorted descending).
    struct RemoveFoliageInstancesFromTileCommand : ICommand<void>
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;
        std::vector<uint32_t> indicesToRemove;

        std::string_view getName() const override { return "RemoveFoliageInstancesFromTile"; }
    };

    // Replace a tile's entire foliage instance set (used by undo/redo snapshots).
    struct SetTileFoliageInstancesCommand : ICommand<void>
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;
        std::vector<::foliage::FoliageInstance> instances;

        std::string_view getName() const override { return "SetTileFoliageInstances"; }
    };

    // ---- VK-1585: procedural MESH scatter (reuses the vegetation ScatterProfile taxonomy) ----

    // The foliage scatter profile lives on TerrainService (like the palette) and persists in a
    // foliage_scatter.json sidecar. Reuses vegetation::ScatterProfile; paletteEntryIndex indexes
    // the FoliageType palette.
    struct SetFoliageScatterProfileCommand : ICommand<void>
    {
        ::vegetation::ScatterProfile profile;

        std::string_view getName() const override { return "SetFoliageScatterProfile"; }
    };

    struct GetFoliageScatterProfileQuery : IQuery<::vegetation::ScatterProfile>
    {
        std::string_view getName() const override { return "GetFoliageScatterProfile"; }
    };

    // Bake procedural foliage MESH instances into terrain tiles as ONE undoable stroke.
    // replaceProcedural (Regenerate) drops existing Procedural-flagged instances in the region;
    // hand-painted foliage is always preserved.
    struct GenerateFoliageScatterCommand : ICommand<void>
    {
        std::optional<::events::vegetation::ScatterRegion> region; // nullopt = whole active terrain
        ::vegetation::ScatterProfile profile;
        uint32_t seed = 1337;
        bool replaceProcedural = true;

        std::string_view getName() const override { return "GenerateFoliageScatter"; }
    };

    // Broadcast when a foliage scatter bake completes — drives the panel's placed-count/budget UI.
    struct FoliageScatterBakeCompletedNotification : INotification
    {
        uint32_t placedCount = 0;    // procedural foliage instances placed this bake
        uint32_t totalCount = 0;     // total foliage instances after the bake (painted + procedural)
        bool budgetExceeded = false; // hit the GPU instance budget

        std::string_view getName() const override { return "FoliageScatterBakeCompleted"; }
    };
}
