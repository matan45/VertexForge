#pragma once
// VK-1585: save a reusable .vfScatterProfile asset (profile file + .vfmeta sidecar). Shared by the
// grass and foliage scatter panels; mirrors ToonProfileEditorWindow::saveProfile. Loading a profile
// is just vegetation::loadScatterProfileFile — no metadata needed on read.
#include "vegetation/VegetationScatterTypes.hpp"
#include "vegetation/ScatterProfileSerialization.hpp"
#include "asset/AssetMetadata.hpp"
#include "asset/AssetMetadataSerializer.hpp"
#include "asset/AssetGUID.hpp"
#include "resource/AssetTypes.hpp"
#include <string>

namespace windows
{
    // Writes `profile` to `path` as a .vfScatterProfile plus its .vfmeta sidecar (so the content
    // browser + game export recognise the asset). Returns false if the profile file write failed.
    inline bool saveScatterProfileAsset(const std::string& path, const vegetation::ScatterProfile& profile)
    {
        if (!vegetation::saveScatterProfileFile(path, profile))
            return false;

        asset::AssetMetadata meta;
        meta.guid = asset::AssetGUID::generate();
        meta.type = resource::AssetType::ScatterProfile;
        meta.importSourcePath = "editor://scatterprofile";
        meta.formatVersion = 1;
        asset::AssetMetadataSerializer::save(meta, asset::AssetMetadataSerializer::getMetaPath(path));
        return true;
    }
}
