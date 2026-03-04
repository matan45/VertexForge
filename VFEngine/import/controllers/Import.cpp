#include "print/Log.hpp"
#include "Import.hpp"
#include <future>
#include <algorithm>
#include "../pipeline/stages/FileValidationStage.hpp"
#include "../pipeline/stages/HeaderReadingStage.hpp"
#include "../pipeline/stages/FileTypeDetectionStage.hpp"
#include "../pipeline/stages/FileProcessingStage.hpp"

namespace controllers
{
    namespace
    {
        ImportFileResult handleFileSuccess(const pipeline::ImportContext& ctx,
                                           ImportProgressCallback& progressCallback,
                                           uint32_t completed, uint32_t totalFiles)
        {
            ImportFileResult fileResult;
            fileResult.success = true;
            fileResult.fileName = ctx.fileName;
            vfLogInfo("Successfully processed: {}", ctx.file.path);

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
        setupPipeline();
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

        vfLogInfo("Import pipeline initialized with {} stages", 4);
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
