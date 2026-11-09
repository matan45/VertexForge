#pragma once
#include "../EventTypes.hpp"
#include "../../data/EQSData.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// VK-1660 EQS template authoring (MCP P11): the validated, hash-guarded, atomic write path for .vfEQSQuery
// files. Handled by services::EQSAuthoringService (Editor only). Paths are absolute engine paths.
// MAIN THREAD ONLY.
namespace events::ai {

    // Reads a .vfEQSQuery's text and content hash from disk.
    struct LoadEQSQueryAssetQuery : IQuery<services::EQSAssetLoadResult> {
        std::string path;

        std::string_view getName() const override { return "LoadEQSQueryAsset"; }
    };

    // Writes `spec` in canonical form (eqs::querySpecToText), checked again with eqs::parseQuerySpec, then
    // publishes AssetSavedNotification (the EQS library reloads the template; the asset database registers it).
    // Refused, writing nothing: a path that is not an absolute .vfEQSQuery path whose file stem is a valid
    // query name (eqs::isValidQueryName); `createNew` with an existing file; `expectedContentHash` set and the
    // file missing or hashing differently; any spec error.
    struct SaveEQSQueryAssetCommand : ICommand<services::EQSAssetSaveResult> {
        std::string path;
        eqs::EQSQuerySpec spec;
        std::optional<uint64_t> expectedContentHash;
        bool createNew = false;

        std::string_view getName() const override { return "SaveEQSQueryAsset"; }
    };

}
