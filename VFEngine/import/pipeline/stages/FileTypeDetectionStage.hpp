#pragma once
#include "../Pipeline.hpp"

namespace pipeline::stages
{
    // Thin stage: delegates detection to the ImporterRegistry (which the
    // built-in importers and any plugin importers populate).
    class FileTypeDetectionStage : public PipelineStage
    {
    public:
        std::optional<ImportContext> process(ImportContext context) override;
        std::string getName() const override { return "FileTypeDetection"; }
    };
}
