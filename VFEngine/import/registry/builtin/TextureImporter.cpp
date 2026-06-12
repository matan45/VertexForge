#include "TextureImporter.hpp"
#include <algorithm>

namespace import::builtin
{
    namespace
    {
        constexpr unsigned char pngSig[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
        constexpr unsigned char jpgSig[] = {0xFF, 0xD8, 0xFF};
        constexpr unsigned char bmpSig[] = {0x42, 0x4D};

        bool matchesSignature(std::span<const unsigned char> header, std::span<const unsigned char> signature)
        {
            return header.size() >= signature.size() &&
                   std::equal(signature.begin(), signature.end(), header.begin());
        }

        // TGA has no magic number; validate the descriptor fields instead.
        // Loosest heuristic in the registry — must keep the lowest priority.
        bool isTGA(std::span<const unsigned char> header)
        {
            if (header.size() < 18) return false;

            uint8_t colorMapType = header[1];
            uint8_t imageType = header[2];
            uint8_t pixelDepth = header[16];

            if (colorMapType > 1) return false;

            if (imageType != 1 && imageType != 2 && imageType != 3 &&
                imageType != 9 && imageType != 10 && imageType != 11)
                return false;

            if (pixelDepth != 8 && pixelDepth != 16 && pixelDepth != 24 && pixelDepth != 32)
                return false;

            uint16_t width = header[12] | (header[13] << 8);
            uint16_t height = header[14] | (header[15] << 8);
            if (width == 0 || height == 0) return false;

            return true;
        }
    }

    std::vector<FormatInfo> TextureImporter::formats() const
    {
        return {
            {"PNG", "Image Files", {"png"}, FileExtension::textrue, resource::AssetType::Texture, 100},
            {"JPEG", "Image Files", {"jpg", "jpeg"}, FileExtension::textrue, resource::AssetType::Texture, 100},
            {"BMP", "Image Files", {"bmp"}, FileExtension::textrue, resource::AssetType::Texture, 100},
            {"TGA", "Image Files", {"tga"}, FileExtension::textrue, resource::AssetType::Texture, 10},
        };
    }

    bool TextureImporter::matches(const std::string& fileType, const DetectionInput& input) const
    {
        if (fileType == "PNG") return matchesSignature(input.header, pngSig);
        if (fileType == "JPEG") return matchesSignature(input.header, jpgSig);
        if (fileType == "BMP") return matchesSignature(input.header, bmpSig);
        if (fileType == "TGA") return isTGA(input.header);
        return false;
    }

    void TextureImporter::process(pipeline::ImportContext& context)
    {
        textureProcessor.loadTextureFile(context.file, context.fileName, context.location,
                                         wrapFileProgress<types::TextureProgressCallback>(context));
    }
}
