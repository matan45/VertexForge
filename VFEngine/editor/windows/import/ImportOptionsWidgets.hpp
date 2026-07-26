#pragma once
#include "config/Config.hpp"
#include "registry/AssetImporter.hpp"
#include <map>
#include <string>
#include <vector>

namespace windows::importui
{
    // Renders one ImGui widget per importer-declared option descriptor, storing the
    // chosen values in `values` keyed by ImportOptionDesc::key. Keys absent from
    // `values` are seeded from the descriptor's defaultValue on first draw, so a
    // caller can pass an empty map (fresh import) or one loaded from a .vfmeta
    // sidecar (reimport) and get the right starting state either way.
    //
    // Returns true if the user changed any value this frame. Draws nothing and
    // returns false for an empty descriptor list.
    bool drawImportOptions(const std::vector<import::ImportOptionDesc>& descs,
                           std::map<std::string, importConfig::ImportOptionValue>& values);
}
