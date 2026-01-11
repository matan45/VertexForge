#pragma once
#include "../Pipeline.hpp"
#include "../../types/Audio.hpp"
#include "../../types/Texture.hpp"
#include "../../types/Mesh.hpp"
#include "../../types/Font.hpp"

namespace pipeline::stages
{
    class FileProcessingStage : public PipelineStage
    {
    public:
        std::optional<ImportContext> process(ImportContext context) override;
        std::string getName() const override { return "FileProcessing"; }

    private:
        types::Audio audioProcessor;
        types::Texture textureProcessor;
        types::Mesh meshProcessor;
        types::Font fontProcessor;

        void processTexture(ImportContext& context);
        void processHDR(ImportContext& context);
        void processAudio(ImportContext& context);
        void processMesh(ImportContext& context);
        void processFont(ImportContext& context);
    };
}