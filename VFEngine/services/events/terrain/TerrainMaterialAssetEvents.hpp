#pragma once
#include "../EventTypes.hpp"
#include "../../data/TerrainAuthoringData.hpp"
#include <cstdint>
#include <optional>
#include <string>

// VK-1653. Authoring a .vfTerrainMat (the terrain's 32-entry material-layer palette) without the
// Terrain Material Editor window.
//
// The window and the paint panel both hold the SHARED cached instance that
// ResourceManager::loadTerrainMaterial returns, so these handlers edit that instance in place
// (snapshot first, roll back if the save fails) rather than writing the file behind its back: an
// open editor window would otherwise keep a stale copy and drop the new layer on its next Save.
//
// A layer change needs no shader compile -- the generated terrain snippet does not depend on the
// layer list -- only TerrainMaterialCompiledNotification, which makes the renderer re-register the
// layer textures (without it registerTerrainLayerTextures early-outs on an unchanged path).

namespace events::terrainMaterial
{
    // Writes a new .vfTerrainMat holding ONE layer (layer 0) built from `baseLayer`, under a unique
    // name in `directory`, then publishes AssetSavedNotification so the asset database assigns a GUID.
    struct CreateTerrainMaterialAssetCommand : ICommand<services::CreateTerrainMaterialAssetResult>
    {
        std::string directory; // absolute
        std::string name;      // file stem; made unique with a numeric suffix
        services::TerrainMaterialLayerPatch baseLayer;

        std::string_view getName() const override { return "CreateTerrainMaterialAsset"; }
    };

    // Appends a layer (index = nullopt) or patches an existing one (index < activeLayerCount), saves,
    // and publishes AssetSavedNotification + TerrainMaterialCompiledNotification.
    struct EditTerrainMaterialLayerCommand : ICommand<services::TerrainMaterialEditResult>
    {
        std::string materialPath; // absolute .vfTerrainMat
        std::optional<uint32_t> index;
        services::TerrainMaterialLayerPatch patch;

        std::string_view getName() const override { return "EditTerrainMaterialLayer"; }
    };

    struct GetTerrainMaterialInfoQuery : IQuery<std::optional<services::TerrainMaterialInfo>>
    {
        std::string materialPath; // absolute .vfTerrainMat

        std::string_view getName() const override { return "GetTerrainMaterialInfo"; }
    };
}
