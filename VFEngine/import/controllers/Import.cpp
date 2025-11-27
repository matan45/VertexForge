#include "Import.hpp"
#include <future>
#include <algorithm>
#include "../pipeline/stages/FileValidationStage.hpp"
#include "../pipeline/stages/HeaderReadingStage.hpp"
#include "../pipeline/stages/FileTypeDetectionStage.hpp"
#include "../pipeline/stages/FileProcessingStage.hpp"
#include "print/EditorLogger.hpp"

namespace controllers
{
    void Import::importFiles(const std::vector<importConfig::ImportFiles>& paths)
    {
        if (!importPipeline)
        {
            initialize();
        }

        vfLogInfo("Starting import of {} files", paths.size());
        
        auto futures = importPipeline->processFiles(paths, location);
        waitForCompletion(std::move(futures));
        
        vfLogInfo("Import process completed");
    }

    void Import::setLocation(std::string_view newLocation)
    {
        location = newLocation;
    }

    void Import::initialize()
    {
        setupPipeline();
    }

    void Import::setupPipeline()
    {
        importPipeline = std::make_unique<pipeline::ImportPipeline>();
        
        // Add pipeline stages in order
        importPipeline->addStage(std::make_unique<pipeline::stages::FileValidationStage>());
        importPipeline->addStage(std::make_unique<pipeline::stages::HeaderReadingStage>());
        importPipeline->addStage(std::make_unique<pipeline::stages::FileTypeDetectionStage>());
        importPipeline->addStage(std::make_unique<pipeline::stages::FileProcessingStage>());
        
        vfLogInfo("Import pipeline initialized with {} stages", 4);
    }

    void Import::waitForCompletion(std::vector<std::future<std::optional<pipeline::ImportContext>>>&& futures)
    {
        size_t successCount = 0;
        size_t failureCount = 0;
        
        for (auto& future : futures)
        {
            try
            {
                auto result = future.get();
                if (result.has_value())
                {
                    successCount++;
                    vfLogInfo("Successfully processed: {}", result->file.path);
                }
                else
                {
                    failureCount++;
                    vfLogError("Failed to process file");
                }
            }
            catch (const std::exception& e)
            {
                failureCount++;
                vfLogError("Exception during file processing: {}", e.what());
            }
        }
        
        vfLogInfo("Import completed: {} successful, {} failed", successCount, failureCount);
    }
}
