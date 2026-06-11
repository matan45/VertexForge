#pragma once
#include "../Pipeline.hpp"

namespace pipeline::stages
{
    // Thin stage: dispatches to the AssetImporter registered for the detected
    // file type. Importers run concurrently on JobSystem workers.
    class FileProcessingStage : public PipelineStage
    {
    public:
        std::optional<ImportContext> process(ImportContext context) override;
        std::string getName() const override { return "FileProcessing"; }
    };
}
