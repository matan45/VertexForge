#pragma once
// VK-1575: CQRS events for authoring the per-tile foliage instance store + FoliageType
// palette (both owned by TerrainService). Mirrors the vegetation tile block in GrassEvents.hpp.
#include "../EventTypes.hpp"
#include "foliage/FoliageTypes.hpp"
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
}
