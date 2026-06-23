#include "print/Log.hpp"
#include "Import.hpp"
#include "threading/JobSystem.hpp"
#include "asset/AssetGUID.hpp"
#include "asset/AssetMetadata.hpp"
#include "asset/AssetMetadataSerializer.hpp"
#include <filesystem>
#include <future>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <thread>
#include "../pipeline/stages/FileValidationStage.hpp"
#include "../pipeline/stages/HeaderReadingStage.hpp"
#include "../pipeline/stages/FileTypeDetectionStage.hpp"
#include "../pipeline/stages/FileProcessingStage.hpp"
#include "../registry/ImporterRegistry.hpp"
#include "../registry/builtin/BuiltinImporters.hpp"

namespace controllers
{
    namespace
    {
        std::string deriveOutputPath(const pipeline::ImportContext& ctx)
        {
            auto& registry = import::ImporterRegistry::instance();

            if (auto* importer = registry.importerFor(ctx.fileType))
            {
                std::string fileName = importer->deriveOutputFile(ctx);
                if (!fileName.empty())
                    return (std::filesystem::path(ctx.location) / fileName).string();
            }

            auto info = registry.formatInfo(ctx.fileType);
            if (!info || info->outputExtension.empty()) return {};

            return (std::filesystem::path(ctx.location) / (ctx.fileName + "." + info->outputExtension)).string();
        }

        resource::AssetType fileTypeToAssetType(const std::string& ft)
        {
            auto info = import::ImporterRegistry::instance().formatInfo(ft);
            return info ? info->assetType : resource::AssetType::COUNT;
        }

        resource::AssetType assetTypeForContext(const pipeline::ImportContext& ctx)
        {
            if (auto* importer = import::ImporterRegistry::instance().importerFor(ctx.fileType))
            {
                auto overridden = importer->deriveAssetType(ctx);
                if (overridden != resource::AssetType::COUNT)
                    return overridden;
            }
            return fileTypeToAssetType(ctx.fileType);
        }

        void createVfMeta(const std::string& outputPath, const std::string& sourcePath,
                          resource::AssetType assetType)
        {
            auto metaPath = asset::AssetMetadataSerializer::getMetaPath(outputPath);
            if (std::filesystem::exists(metaPath))
                return;

            asset::AssetMetadata metadata;
            metadata.guid = asset::AssetGUID::generate();
            metadata.type = assetType;
            metadata.importSourcePath = sourcePath;

            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &time);
#else
            localtime_r(&time, &tm);
#endif
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
            metadata.importTimestamp = buf;

            asset::AssetMetadataSerializer::save(metadata, metaPath);
        }

        std::vector<ImportFileResult> handleFileSuccess(const pipeline::ImportContext& ctx,
                                                        ImportProgressCallback& progressCallback,
                                                        uint32_t completed, uint32_t totalFiles)
        {
            vfLogInfo("Successfully processed: {}", ctx.file.path);

            const resource::AssetType assetType = assetTypeForContext(ctx);

            std::vector<ImportFileResult> results;

            auto addResult = [&](const std::string& outputPath)
            {
                ImportFileResult fileResult;
                fileResult.success = true;
                fileResult.fileName = outputPath.empty()
                                          ? ctx.fileName
                                          : std::filesystem::path(outputPath).stem().string();
                fileResult.sourcePath = ctx.file.path;
                fileResult.outputPath = outputPath;
                fileResult.fileType = ctx.fileType;

                // Create .vfmeta sidecar with importSource and importTimestamp.
                // Existence guard: config-dependent outputs (e.g. animation-only
                // mesh import of a file without animations) may not be written.
                if (!outputPath.empty() && std::filesystem::exists(outputPath) &&
                    assetType != resource::AssetType::COUNT)
                {
                    createVfMeta(outputPath, ctx.file.path, assetType);
                }

                results.push_back(std::move(fileResult));
            };

            if (!ctx.outputFiles.empty())
            {
                // One input file emitted multiple engine assets (e.g. one
                // .vfMesh per mesh in the model): a .vfmeta and a result each.
                for (const auto& outputPath : ctx.outputFiles)
                    addResult(outputPath);
            }
            else
            {
                addResult(deriveOutputPath(ctx));
            }

            if (progressCallback)
            {
                progressCallback(ctx.fileName, completed, totalFiles, 1.0f);
            }

            return results;
        }

        ImportFileResult handleFileFailure(ImportProgressCallback& progressCallback,
                                            uint32_t completed, uint32_t totalFiles)
        {
            ImportFileResult fileResult;
            fileResult.success = false;
            fileResult.errorMessage = "Failed to process file";
            vfLogError("Failed to process file");

            if (progressCallback)
            {
                progressCallback("unknown", completed, totalFiles, 1.0f);
            }

            return fileResult;
        }

        ImportFileResult handleFileException(const std::exception& e,
                                              ImportProgressCallback& progressCallback,
                                              uint32_t completed, uint32_t totalFiles)
        {
            ImportFileResult fileResult;
            fileResult.success = false;
            fileResult.errorMessage = e.what();
            vfLogError("Exception during file processing: {}", e.what());

            if (progressCallback)
            {
                progressCallback("error", completed, totalFiles, 1.0f);
            }

            return fileResult;
        }
    }

    ImportResult Import::importFiles(const std::vector<importConfig::ImportFiles>& paths,
                                     ImportProgressCallback progressCallback)
    {
        ImportResult result;
        if (paths.empty()) return result;

        if (!importPipeline)
        {
            initialize();
        }

        // Tracks the whole synchronous run so waitForIdle() can drain
        // in-flight imports before importers are unregistered.
        struct ActiveImportScope
        {
            ActiveImportScope() { activeImports.fetch_add(1); }
            ~ActiveImportScope() { activeImports.fetch_sub(1); }
        } activeScope;

        vfLogInfo("Starting import of {} files", paths.size());

        auto futures = importPipeline->processFiles(paths, location, progressCallback);
        result = waitForCompletion(std::move(futures), progressCallback, static_cast<uint32_t>(paths.size()), paths);

        vfLogInfo("Import process completed");
        return result;
    }

    void Import::setLocation(std::string_view newLocation)
    {
        location = newLocation;
    }

    void Import::initialize()
    {
        // Import DLL has its own copy of Utilities (static lib), so its
        // JobSystem singleton needs separate initialization.
        threading::JobSystem::instance().init();
        import::builtin::ensureRegistered();
        setupPipeline();
    }

    void Import::shutdown()
    {
        // Plugin-owned importers must go before plugin DLLs unload — drain any
        // running import first so no worker is inside a plugin vtable.
        waitForIdle();
        import::ImporterRegistry::instance().unregisterAllExceptOwner("engine");

        importPipeline.reset();
        threading::JobSystem::instance().shutdown();
    }

    void Import::addCustomStage(std::unique_ptr<pipeline::PipelineStage> stage)
    {
        if (!importPipeline)
        {
            initialize();
        }

        vfLogInfo("Adding custom import pipeline stage: {}", stage->getName());
        importPipeline->addStage(std::move(stage));
    }

    void Import::requestCancel()
    {
        cancelRequested.store(true);
        vfLogInfo("Import cancellation requested");
    }

    bool Import::isCancellationRequested()
    {
        return cancelRequested.load();
    }

    void Import::resetCancellation()
    {
        cancelRequested.store(false);
    }

    std::atomic<bool>* Import::getCancelFlag()
    {
        return &cancelRequested;
    }

    std::vector<import::FormatInfo> Import::supportedFormats()
    {
        import::builtin::ensureRegistered();
        return import::ImporterRegistry::instance().allFormats();
    }

    resource::AssetType Import::assetTypeFor(const std::string& fileType)
    {
        import::builtin::ensureRegistered();
        return fileTypeToAssetType(fileType);
    }

    std::vector<import::ImportOptionDesc> Import::optionsForExtension(const std::string& extension)
    {
        import::builtin::ensureRegistered();
        return import::ImporterRegistry::instance().optionsForExtension(extension);
    }

    void Import::registerImporter(std::unique_ptr<import::AssetImporter> importer,
                                  std::string_view ownerTag)
    {
        import::builtin::ensureRegistered();
        import::ImporterRegistry::instance().registerImporter(std::move(importer), ownerTag);
    }

    void Import::unregisterImportersByOwner(std::string_view ownerTag)
    {
        waitForIdle();
        import::ImporterRegistry::instance().unregisterByOwner(ownerTag);
    }

    void Import::waitForIdle()
    {
        if (activeImports.load() == 0)
            return;

        requestCancel();
        while (activeImports.load() != 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        resetCancellation();
    }

    void Import::setupPipeline()
    {
        importPipeline = std::make_unique<pipeline::ImportPipeline>();

        importPipeline->addStage(std::make_unique<pipeline::stages::FileValidationStage>());
        importPipeline->addStage(std::make_unique<pipeline::stages::HeaderReadingStage>());
        importPipeline->addStage(std::make_unique<pipeline::stages::FileTypeDetectionStage>());
        importPipeline->addStage(std::make_unique<pipeline::stages::FileProcessingStage>());

    }

    ImportResult Import::waitForCompletion(std::vector<std::future<std::optional<pipeline::ImportContext>>>&& futures,
                                           ImportProgressCallback progressCallback,
                                           uint32_t totalFiles,
                                           const std::vector<importConfig::ImportFiles>& originalPaths)
    {
        ImportResult result;
        uint32_t completed = 0;

        for (size_t i = 0; i < futures.size(); ++i)
        {
            const std::string sourcePath = (i < originalPaths.size()) ? originalPaths[i].path : "";

            try
            {
                auto futureResult = futures[i].get();
                completed++;

                if (futureResult.has_value())
                {
                    // A single input file may now yield several output assets
                    // (one .vfMesh per mesh), so success produces N results.
                    auto successResults = handleFileSuccess(*futureResult, progressCallback,
                                                            completed, totalFiles);
                    result.successCount += successResults.size();
                    for (auto& r : successResults)
                    {
                        r.sourcePath = sourcePath;
                        result.fileResults.push_back(std::move(r));
                    }
                }
                else
                {
                    ImportFileResult fileResult = handleFileFailure(progressCallback, completed, totalFiles);
                    fileResult.sourcePath = sourcePath;
                    result.failureCount++;
                    result.fileResults.push_back(std::move(fileResult));
                }
            }
            catch (const std::exception& e)
            {
                completed++;
                ImportFileResult fileResult = handleFileException(e, progressCallback, completed, totalFiles);
                fileResult.sourcePath = sourcePath;
                result.failureCount++;
                result.fileResults.push_back(std::move(fileResult));
            }
        }

        vfLogInfo("Import completed: {} successful, {} failed", result.successCount, result.failureCount);
        return result;
    }
}
