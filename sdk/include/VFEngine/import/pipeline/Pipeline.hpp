#pragma once
#include <vector>
#include <memory>
#include <future>
#include <optional>
#include <functional>
#include "config/Config.hpp"

namespace controllers {
    // Forward declare the callback type
    using ImportProgressCallback = std::function<void(
        std::string_view currentFile,
        uint32_t fileIndex,
        uint32_t totalFiles,
        float fileProgress
    )>;
}

namespace pipeline
{
    struct ImportContext
    {
        importConfig::ImportFiles file;
        std::string fileName;
        std::string location;
        std::vector<unsigned char> header;
        std::string fileType;
        bool isValid = true;
        std::string errorMessage;
        uint32_t fileIndex = 0;
        uint32_t totalFiles = 0;
        controllers::ImportProgressCallback progressCallback;

        // Engine assets written by the importer's process(). Empty => the
        // controller falls back to the single deriveOutputPath(). Lets one
        // input file emit N outputs (e.g. one .vfMesh per mesh in the model).
        std::vector<std::string> outputFiles;

        // Constructor with file and location (required since ImportFiles has explicit constructor)
        ImportContext(const importConfig::ImportFiles& f, std::string_view loc)
            : file(f), location(loc) {}

        // Move constructor and assignment
        ImportContext(ImportContext&&) = default;
        ImportContext& operator=(ImportContext&&) = default;

        // Copy constructor and assignment
        ImportContext(const ImportContext&) = default;
        ImportContext& operator=(const ImportContext&) = default;

        // Delete default constructor since ImportFiles requires explicit construction
        ImportContext() = delete;
    };

    class PipelineStage
    {
    public:
        virtual ~PipelineStage() = default;
        virtual std::optional<ImportContext> process(ImportContext context) = 0;
        virtual std::string getName() const = 0;
    };

    class ImportPipeline
    {
    private:
        std::vector<std::unique_ptr<PipelineStage>> stages;

    public:
        void addStage(std::unique_ptr<PipelineStage> stage);
        std::vector<std::future<std::optional<ImportContext>>> processFiles(
            const std::vector<importConfig::ImportFiles>& files,
            std::string_view location,
            controllers::ImportProgressCallback progressCallback = nullptr);

    private:
        std::optional<ImportContext> processFile(importConfig::ImportFiles file,
                                                  std::string_view location,
                                                  uint32_t fileIndex,
                                                  uint32_t totalFiles,
                                                  controllers::ImportProgressCallback progressCallback);
    };
}