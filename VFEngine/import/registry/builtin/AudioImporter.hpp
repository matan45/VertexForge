#pragma once
#include "../AssetImporter.hpp"
#include "../../types/Audio.hpp"

namespace import::builtin
{
    // MP3 / WAV / OGG -> .vfAudio
    class AudioImporter : public AssetImporter
    {
    public:
        std::vector<FormatInfo> formats() const override;
        bool matches(const std::string& fileType, const DetectionInput& input) const override;
        void process(pipeline::ImportContext& context) override;

    private:
        types::Audio audioProcessor;
    };
}
