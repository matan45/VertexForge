#include "ContentBrowserReimport.hpp"
#include "Import.hpp"
#include "asset/AssetMetadataSerializer.hpp"
#include "resource/FontResource.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ResourceEvents.hpp"
#include "print/Log.hpp"
#include "string/StringUtil.hpp"

#include <algorithm>
#include <thread>
#include <utility>

namespace fs = std::filesystem;

namespace windows::reimport
{
    namespace
    {
        // AssetMetadata::kCurrentFormatVersion when "importOptions" was added (VK-1629).
        // A sidecar older than this never recorded options, whatever the importer offers.
        constexpr uint32_t kImportOptionsFormatVersion = 4;

        std::string lowerExtensionNoDot(const fs::path& path)
        {
            std::string ext = path.extension().string();
            if (!ext.empty() && ext.front() == '.')
                ext.erase(0, 1);
            return StringUtil::toLower(ext);
        }

        // ImportConfig carries settings that predate the importer-declared option
        // channel — texture compression mode/quality, audio quality/load type/mono,
        // and the whole mesh + VHACD + fracture block. Those are chosen in the
        // import dialog and never written to the sidecar, so a reimport of one of
        // these types falls back to the ImportConfig defaults. Fonts are unaffected:
        // every font knob is a declared option.
        bool typeHasUnpersistedSettings(resource::AssetType type)
        {
            switch (type)
            {
                case resource::AssetType::Texture:
                case resource::AssetType::HDR:
                case resource::AssetType::Audio:
                case resource::AssetType::Mesh:
                case resource::AssetType::Animation:
                    return true;
                default:
                    return false;
            }
        }
    }

    Eligibility evaluate(const fs::path& assetPath)
    {
        Eligibility result;

        if (assetPath.empty() || !fs::is_regular_file(assetPath))
            return result;

        // Independent of every eligibility test below — a stale asset needs reimporting
        // whether or not one is currently possible, and nothing else in the editor says
        // so out loud.
        if (StringUtil::toLower(assetPath.extension().string()) == ".vffont")
        {
            result.staleFormat =
                resource::FontResource::probeHeader(assetPath.string()) !=
                resource::FontHeaderStatus::Ok;
        }

        const auto metadata =
            asset::AssetMetadataSerializer::load(asset::AssetMetadataSerializer::getMetaPath(assetPath));
        if (!metadata)
        {
            result.reason = "No .vfmeta sidecar — nothing records where this asset came from";
            return result;
        }

        if (metadata->importSourcePath.empty())
        {
            result.reason = "Authored in the editor, not imported from a source file";
            return result;
        }

        // BRDFLUTExporter and friends stamp a "generated:<name>" pseudo-source.
        if (metadata->importSourcePath.rfind("generated:", 0) == 0)
        {
            result.reason = "Engine-generated asset (" + metadata->importSourcePath + ")";
            return result;
        }

        const fs::path source(metadata->importSourcePath);
        if (!fs::is_regular_file(source))
        {
            result.reason = "Source file is missing: " + metadata->importSourcePath;
            return result;
        }

        // The importer names its output from the SOURCE stem and writes it into the
        // import location (see ImportContext::fileName / deriveOutputPath), so a
        // reimport only lands back on this exact file when the stem and the produced
        // extension both line up. Otherwise it would write a differently named asset
        // and leave this one stale — which is what happens for a renamed asset, for
        // one of several split outputs (model_Cube.vfMesh), and for a texture
        // extracted from a model (whose recorded source is the model).
        const std::string sourceExt = lowerExtensionNoDot(source);
        const std::string assetExt = lowerExtensionNoDot(assetPath);

        const auto formats = controllers::Import::supportedFormats();
        const auto format = std::ranges::find_if(formats, [&](const import::FormatInfo& info)
        {
            return std::ranges::find(info.extensions, sourceExt) != info.extensions.end();
        });

        if (format == formats.end())
        {
            result.reason = "No importer handles ." + sourceExt + " any more";
            return result;
        }

        if (StringUtil::toLower(format->outputExtension) != assetExt)
        {
            result.reason = "Reimporting " + source.filename().string() + " produces a ." +
                format->outputExtension + ", not this asset";
            return result;
        }

        // Case-insensitive: Windows filenames are, and the importer round-trips the
        // stem through the filesystem.
        if (StringUtil::toLower(source.stem().string()) != StringUtil::toLower(assetPath.stem().string()))
        {
            result.reason = "Reimport would write " + source.stem().string() + "." + assetExt +
                " instead of this file — rename it back or reimport the source directly";
            return result;
        }

        result.enabled = true;
        result.sourcePath = metadata->importSourcePath;
        result.storedOptions = metadata->importOptions;
        result.descs = controllers::Import::optionsForExtension(sourceExt);
        result.hasUnpersistedSettings = typeHasUnpersistedSettings(metadata->type);

        // An empty storedOptions map is ambiguous: either the importer declares no
        // options, or the sidecar predates the version that started recording them
        // (AssetMetadata v4, VK-1629). Only the second case silently swaps the settings
        // the asset was actually baked with for importer defaults, so flag it.
        result.optionsUnavailable =
            metadata->formatVersion < kImportOptionsFormatVersion && !result.descs.empty();
        return result;
    }

    void run(const std::string& sourcePath, const fs::path& destDir,
             const std::map<std::string, importConfig::ImportOptionValue>& options)
    {
        importConfig::ImportConfig config;
        config.customOptions = options;

        auto& dispatcher = events::EventDispatcher::instance();

        events::resource::ImportStartedNotification startNotif;
        startNotif.files = {sourcePath};
        dispatcher.publish(startNotif);

        controllers::Import::resetCancellation();

        // Same shape as the import dialog: a detached worker so the UI keeps
        // drawing, with progress and completion surfaced through notifications.
        std::thread([sourcePath, destination = destDir.string(), config = std::move(config)]()
        {
            auto& threadDispatcher = events::EventDispatcher::instance();

            // Import::location is a process-wide static that normally tracks the
            // content browser's directory. Setting it here — rather than before the
            // thread starts — means navigating away cannot retarget this reimport.
            controllers::Import::setLocation(destination);

            auto progressCallback = [&threadDispatcher](std::string_view currentFile,
                                                        uint32_t /*fileIndex*/,
                                                        uint32_t /*totalFiles*/,
                                                        float fileProgress)
            {
                events::resource::ImportProgressNotification progressNotif;
                progressNotif.currentFile = std::string(currentFile);
                progressNotif.progress = fileProgress;
                threadDispatcher.publish(progressNotif);
            };

            const auto result = controllers::Import::importFiles(
                {importConfig::ImportFiles(sourcePath, config)}, progressCallback);

            events::resource::ImportCompletedNotification completeNotif;
            for (const auto& fileResult : result.fileResults)
            {
                services::ImportResult res;
                res.sourcePath = fileResult.sourcePath;
                res.outputPath = fileResult.outputPath;
                res.assetType = controllers::Import::assetTypeFor(fileResult.fileType);
                res.success = fileResult.success;
                res.errorMessage = fileResult.errorMessage;
                completeNotif.results.push_back(res);
            }
            threadDispatcher.publish(completeNotif);

            vfLogInfo("Reimported {} ({} succeeded, {} failed)", sourcePath,
                      result.successCount, result.failureCount);
        }).detach();
    }
}
