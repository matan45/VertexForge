#pragma once
#include "../Pipeline.hpp"

namespace pipeline::stages
{
    class HeaderReadingStage : public PipelineStage
    {
    public:
        std::optional<ImportContext> process(ImportContext context) override;
        std::string getName() const override { return "HeaderReading"; }

    private:
        static constexpr size_t HEADER_SIZE = 32; // Read enough bytes for all format checks
    };
}