#pragma once

#include <asset/AssetGUID.hpp>
#include <string>

namespace services
{
    // Creates or refreshes the .vfmeta sidecar for a saved .vfTerrain and registers the asset in
    // the AssetDatabase, returning the GUID so the caller can point TerrainComponent::terrainRef
    // at it. An existing sidecar's GUID is preserved; otherwise a fresh one is minted.
    //
    // Both the full and the incremental save route through here so the two write an identical
    // metadata contract by construction rather than by inspection (VK-1643).
    //
    // Call only after the terrain binary has been written successfully — on failure the sidecar
    // is left alone, which keeps data and metadata mutually consistent. Returns an invalid GUID
    // if the sidecar could not be written; the terrain file itself is still valid in that case.
    asset::AssetGUID refreshTerrainSidecar(const std::string& terrainPath);
}
