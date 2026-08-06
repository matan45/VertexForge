#pragma once

#include <asset/AssetGUID.hpp>
#include <string>

namespace services
{
    // Resolves the GUID a terrain will be saved under, WITHOUT writing anything.
    //
    // VK-1646 needs the GUID before the save, because the `.vfterrainlayers` sidecar stamps it and
    // the sidecar is written as part of the same commit. refreshTerrainSidecar() cannot supply it:
    // it deliberately runs only after the binary is on disk, and on a terrain's first save there is
    // no `.vfmeta` to read a GUID out of yet.
    //
    // Returns the existing sidecar's GUID when there is one, so calling this and then
    // refreshTerrainSidecar() with the result is idempotent for an already-registered asset.
    asset::AssetGUID peekOrMintTerrainGuid(const std::string& terrainPath);

    // Creates or refreshes the .vfmeta sidecar for a saved .vfTerrain and registers the asset in
    // the AssetDatabase, returning the GUID so the caller can point TerrainComponent::terrainRef
    // at it. An existing sidecar's GUID is preserved; otherwise a fresh one is minted.
    //
    // Pass the value peekOrMintTerrainGuid() returned so a first save persists the same GUID it
    // already stamped into the layer sidecar. An invalid `minted` keeps the pre-VK-1646 behaviour
    // of minting here.
    //
    // Both the full and the incremental save route through here so the two write an identical
    // metadata contract by construction rather than by inspection (VK-1643).
    //
    // Call only after the terrain binary has been written successfully — on failure the sidecar
    // is left alone, which keeps data and metadata mutually consistent. Returns an invalid GUID
    // if the sidecar could not be written; the terrain file itself is still valid in that case.
    asset::AssetGUID refreshTerrainSidecar(const std::string& terrainPath,
                                           asset::AssetGUID minted = {});
}
