#include "Pipeline.hpp"
#include "print/EditorLogger.hpp"
#include <future>

namespace pipeline
{
    void ImportPipeline::addStage(std::unique_ptr<PipelineStage> stage)
    {
        stages.push_back(std::move(stage));
    }

    std::vector<std::future<std::optional<ImportContext>>> ImportPipeline::processFiles(
        const std::vector<importConfig::ImportFiles>& files, 
        std::string_view location)
    {
        std::vector<std::future<std::optional<ImportContext>>> futures;
        futures.reserve(files.size());

        for (const auto& file : files)
        {
            futures.push_back(std::async(std::launch::async, 
                [this, file, location]() -> std::optional<ImportContext> {
                    return processFile(file, location);
                }));
        }

        return futures;
    }

    std::optional<ImportContext> ImportPipeline::processFile(importConfig::ImportFiles file, std::string_view location)
    {
        ImportContext context(file, location);

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