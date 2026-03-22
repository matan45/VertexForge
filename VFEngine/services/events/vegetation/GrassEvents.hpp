#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "vegetation/GrassConfig.hpp"
#include "vegetation/VegetationTypes.hpp"
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

    // Remove billboard instances from a specific tile by indices (sorted descending)
    struct RemoveBillboardInstancesFromTileCommand : ICommand<void>
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;
        std::vector<uint32_t> indicesToRemove; // Must be sorted descending

        std::string_view getName() const override { return "RemoveBillboardInstancesFromTile"; }
    };
}
