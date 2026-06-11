#pragma once
#include "../AssetImporter.hpp"

namespace import::builtin
{
    // 16-bit heightmaps (16-bit grayscale PNG, RAW16/R16) -> .vfImage
    //
    // The output is a standard uncompressed single-mip .vfImage so terrain
    // tooling keeps working unchanged, with the 16-bit height packed as
    // R=G=B = high byte, A = low byte. HeightmapLoader::loadVFImage decodes
    // the full 16 bits when the alpha plane varies (a plain image has a
    // constant 255 alpha and keeps the 8-bit luminance path).
    class HeightmapImporter : public AssetImporter
    {
    public:
        std::vector<FormatInfo> formats() const override;
        bool matches(const std::string& fileType, const DetectionInput& input) const override;
        void process(pipeline::ImportContext& context) override;
    };
}
