#pragma once
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include "config/Config.hpp"
#include "../pipeline/Pipeline.hpp"

namespace controllers
{
    // Progress callback: (currentFile, fileIndex, totalFiles, fileProgress)
    // fileProgress: 0.0-1.0 for current file's sub-progress (e.g., mesh processing)
    using ImportProgressCallback = std::function<void(
        std::string_view currentFile,
        uint32_t fileIndex,
        uint32_t totalFiles,
        float fileProgress
    )>;

    // Result of a single file import
    struct ImportFileResult
    {
        std::string sourcePath;
        std::string fileName;
        bool success = false;
        std::string errorMessage;
    };

    // Result of the entire import operation
    struct ImportResult
    {
        std::vector<ImportFileResult> fileResults;
        size_t successCount = 0;
        size_t failureCount = 0;
    };

    class Import
    {
    private:
        inline static std::string location;
        inline static std::unique_ptr<pipeline::ImportPipeline> importPipeline;

    public:
        static ImportResult importFiles(const std::vector<importConfig::ImportFiles>& paths,
                                        ImportProgressCallback progressCallback = nullptr);
        static void setLocation(std::string_view newLocation);
        static void initialize();

    private:
        static void setupPipeline();
        static ImportResult waitForCompletion(std::vector<std::future<std::optional<pipeline::ImportContext>>>&& futures,
                                              ImportProgressCallback progressCallback,
                                              uint32_t totalFiles,
                                              const std::vector<importConfig::ImportFiles>& originalPaths);
    };
}
