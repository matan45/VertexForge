#pragma once
#include "../AssetImporter.hpp"
#include "../../types/Texture.hpp"

namespace import::builtin
{
    // HDR (Radiance) / EXR -> .vfHdr
    class HdrImporter : public AssetImporter
    {
    public:
        std::vector<FormatInfo> formats() const override;
        bool matches(const std::string& fileType, const DetectionInput& input) const override;
        void process(pipeline::ImportContext& context) override;

    private:
        types::Texture textureProcessor;
    };
}
