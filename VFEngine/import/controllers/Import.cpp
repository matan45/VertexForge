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
#include "../pipeline/stages/FileValidationStage.hpp"
#include "../pipeline/stages/HeaderReadingStage.hpp"
#include "../pipeline/stages/FileTypeDetectionStage.hpp"
#include "../pipeline/stages/FileProcessingStage.hpp"

namespace controllers
{
    namespace
    {
        std::string deriveOutputPath(const pipeline::ImportContext& ctx)
        {
            std::string ext;
            std::string ft = ctx.fileType;

            if (ft == "PNG" || ft == "JPEG" || ft == "BMP" || ft == "TGA")
                ext = FileExtension::textrue;
            else if (ft == "HDR" || ft == "EXR")
                ext = FileExtension::hdr;
            else if (ft == "MP3" || ft == "WAV" || ft == "OGG")
                ext = FileExtension::audio;
            else if (ft == "OBJ" || ft == "FBX" || ft == "DAE" || ft == "GLTF" || ft == "GLB")
                ext = FileExtension::mesh;
            else if (ft == "TTF" || ft == "OTF")
                ext = FileExtension::font;

            if (ext.empty()) return {};

            return (std::filesystem::path(ctx.location) / (ctx.fileName + "." + ext)).string();
        }

        resource::AssetType fileTypeToAssetType(const std::string& ft)
        {
            if (ft == "PNG" || ft == "JPEG" || ft == "BMP" || ft == "TGA")
                return resource::AssetType::Texture;
            if (ft == "HDR" || ft == "EXR")
                return resource::AssetType::HDR;
            if (ft == "MP3" || ft == "WAV" || ft == "OGG")
                return resource::AssetType::Audio;
            if (ft == "OBJ" || ft == "FBX" || ft == "DAE" || ft == "GLTF" || ft == "GLB")
                return resource::AssetType::Mesh;
            if (ft == "TTF" || ft == "OTF")
                return resource::AssetType::Font;
            return resource::AssetType::COUNT;
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

        ImportFileResult handleFileSuccess(const pipeline::ImportContext& ctx,
                                           ImportProgressCallback& progressCallback,
                                           uint32_t completed, uint32_t totalFiles)
        {
            ImportFileResult fileResult;
            fileResult.success = true;
            fileResult.fileName = ctx.fileName;
            fileResult.sourcePath = ctx.file.path;
            fileResult.outputPath = deriveOutputPath(ctx);
            fileResult.fileType = ctx.fileType;
            vfLogInfo("Successfully processed: {}", ctx.file.path);

            // Create .vfmeta sidecar with importSource and importTimestamp
            if (!fileResult.outputPath.empty())
            {
                resource::AssetType assetType = fileTypeToAssetType(ctx.fileType);
                if (assetType != resource::AssetType::COUNT)
                {
                    createVfMeta(fileResult.outputPath, ctx.file.path, assetType);
                }

                // If SVT was generated alongside, create its .vfmeta too
                if (ctx.file.config.svtEnabled)
                {
                    std::string svtPath = std::string(ctx.location) + "/" +
                                          std::string(ctx.fileName) + ".vfSVT";
                    if (std::filesystem::exists(svtPath))
                    {
                        createVfMeta(svtPath, ctx.file.path, resource::AssetType::SVT);
                    }
                }
            }

            if (progressCallback)
            {
                progressCallback(ctx.fileName, completed, totalFiles, 1.0f);
            }

            return fileResult;
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
        setupPipeline();
    }

    void Import::shutdown()
    {
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
            ImportFileResult fileResult;
            fileResult.sourcePath = (i < originalPaths.size()) ? originalPaths[i].path : "";

            try
            {
                auto futureResult = futures[i].get();
                completed++;

                if (futureResult.has_value())
                {
                    fileResult = handleFileSuccess(*futureResult, progressCallback, completed, totalFiles);
                    result.successCount++;
                }
                else
                {
                    fileResult = handleFileFailure(progressCallback, completed, totalFiles);
                    result.failureCount++;
                }
            }
            catch (const std::exception& e)
            {
                completed++;
                fileResult = handleFileException(e, progressCallback, completed, totalFiles);
                result.failureCount++;
            }

            fileResult.sourcePath = (i < originalPaths.size()) ? originalPaths[i].path : "";
            result.fileResults.push_back(std::move(fileResult));
        }

        vfLogInfo("Import completed: {} successful, {} failed", result.successCount, result.failureCount);
        return result;
    }
}
