#pragma once
#include "config/Config.hpp"
#include "registry/AssetImporter.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace windows::reimport
{
    // Whether an engine asset can be regenerated from the source file recorded in
    // its .vfmeta, and what to feed the importer if so.
    struct Eligibility
    {
        bool enabled = false;
        std::string sourcePath;     // the recorded source, absolute or project-relative
        std::string reason;         // why it is unavailable; shown as the menu tooltip
        std::vector<import::ImportOptionDesc> descs;                          // importer-declared options
        std::map<std::string, importConfig::ImportOptionValue> storedOptions;  // as baked, from the sidecar
        bool hasUnpersistedSettings = false; // type also has non-customOptions settings (see .cpp)
    };

    // Reads the sidecar next to `assetPath` and decides whether Reimport applies.
    // Cheap enough to call while building a context menu (one small JSON read plus
    // a registry query), but not per frame.
    Eligibility evaluate(const std::filesystem::path& assetPath);

    // Re-runs the importer on `sourcePath`, writing into `destDir` with `options`.
    // Publishes the same Import{Started,Progress,Completed} notifications as the
    // import dialog, so the progress window and the content browser refresh
    // themselves. Returns immediately; the import runs on a detached thread.
    void run(const std::string& sourcePath, const std::filesystem::path& destDir,
             const std::map<std::string, importConfig::ImportOptionValue>& options);
}
