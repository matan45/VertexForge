#include "print/Log.hpp"
#include "Pipeline.hpp"
#include "threading/JobSystem.hpp"

namespace pipeline
{
    void ImportPipeline::addStage(std::unique_ptr<PipelineStage> stage)
    {
        stages.push_back(std::move(stage));
    }

    std::vector<std::future<std::optional<ImportContext>>> ImportPipeline::processFiles(
        const std::vector<importConfig::ImportFiles>& files,
        std::string_view location,
        controllers::ImportProgressCallback progressCallback)
    {
        std::vector<std::future<std::optional<ImportContext>>> futures;
        futures.reserve(files.size());

        uint32_t totalFiles = static_cast<uint32_t>(files.size());
        uint32_t fileIndex = 0;

        for (const auto& file : files)
        {
            futures.push_back(threading::JobSystem::instance().submit(
                [this, file, location, fileIndex, totalFiles, progressCallback]() -> std::optional<ImportContext> {
                    return processFile(file, location, fileIndex, totalFiles, progressCallback);
                }, threading::JobPriority::NORMAL));
            fileIndex++;
        }
        return futures;
    }

    std::optional<ImportContext> ImportPipeline::processFile(importConfig::ImportFiles file,
                                                              std::string_view location,
                                                              uint32_t fileIndex,
                                                              uint32_t totalFiles,
                                                              controllers::ImportProgressCallback progressCallback)
    {
        ImportContext context(file, location);
        context.fileIndex = fileIndex;
        context.totalFiles = totalFiles;
        context.progressCallback = progressCallback;

        // Report initial progress for this file
        if (progressCallback)
        {
            progressCallback(file.path, fileIndex + 1, totalFiles, 0.0f);
        }

        try
        {
            for (const auto& stage : stages)
            {
                vfLogInfo("Processing file {} through stage: {}", file.path, stage->getName());
                
                auto result = stage->process(std::move(context));
                if (!result.has_value())
                {
                    vfLogError("Pipeline stage {} failed for file {}", stage->getName(), file.path);
                    return std::nullopt;
                }
                
                context = std::move(result.value());
                
                if (!context.isValid)
                {
                    vfLogError("Pipeline validation failed at stage {} for file {}: {}", 
                        stage->getName(), file.path, context.errorMessage);
                    return std::nullopt;
                }
            }
            
            vfLogInfo("Successfully processed file: {}", file.path);
            return context;
        }
        catch (const std::exception& e)
        {
            vfLogError("Pipeline exception for file {}: {}", file.path, e.what());
            return std::nullopt;
        }
    }
}