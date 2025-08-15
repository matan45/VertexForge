#pragma once
#include "../Pipeline.hpp"

namespace pipeline::stages
{
    class FileValidationStage : public PipelineStage
    {
    public:
        std::optional<ImportContext> process(ImportContext context) override;
        std::string getName() const override { return "FileValidation"; }
    };
}