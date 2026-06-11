#include "FontImporter.hpp"
#include <algorithm>
#include <stdexcept>

namespace import::builtin
{
    namespace
    {
        constexpr unsigned char otfSig[] = {0x4F, 0x54, 0x54, 0x4F};

        bool matchesSignature(std::span<const unsigned char> header, std::span<const unsigned char> signature)
        {
            return header.size() >= signature.size() &&
                   std::equal(signature.begin(), signature.end(), header.begin());
        }

        bool isTTF(std::span<const unsigned char> header)
        {
            if (header.size() < 4) return false;

            // Standard TrueType: 00 01 00 00
            if (header[0] == 0x00 && header[1] == 0x01 &&
                header[2] == 0x00 && header[3] == 0x00)
                return true;

            // Apple TrueType: 'true' (74 72 75 65)
            if (header[0] == 0x74 && header[1] == 0x72 &&
                header[2] == 0x75 && header[3] == 0x65)
                return true;

            // TrueType Collection: 'ttcf' (74 74 63 66)
            if (header[0] == 0x74 && header[1] == 0x74 &&
                header[2] == 0x63 && header[3] == 0x66)
                return true;

            return false;
        }
    }

    std::vector<FormatInfo> FontImporter::formats() const
    {
        return {
            {"OTF", "Font Files", {"otf"}, FileExtension::font, resource::AssetType::Font, 100},
            {"TTF", "Font Files", {"ttf"}, FileExtension::font, resource::AssetType::Font, 15},
        };
    }

    bool FontImporter::matches(const std::string& fileType, const DetectionInput& input) const
    {
        if (fileType == "OTF") return matchesSignature(input.header, otfSig);
        if (fileType == "TTF") return isTTF(input.header);
        return false;
    }

    void FontImporter::process(pipeline::ImportContext& context)
    {
        types::FontImportConfig config;
        if (!fontProcessor.loadFromFile(context.file, context.fileName, context.location, config,
                                        wrapFileProgress<types::FontProgressCallback>(context)))
        {
            throw std::runtime_error("Failed to import font: " + std::string(context.fileName));
        }
    }
}
