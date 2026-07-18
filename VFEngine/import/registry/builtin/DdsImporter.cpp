#include "DdsImporter.hpp"
#include <algorithm>
#include <filesystem>
#include <span>
#include <stdexcept>

namespace import::builtin
{
    namespace
    {
        // "DDS " magic.
        constexpr unsigned char ddsSig[] = {0x44, 0x44, 0x53, 0x20};

        bool matchesSignature(std::span<const unsigned char> header, std::span<const unsigned char> signature)
        {
            return header.size() >= signature.size() &&
                   std::equal(signature.begin(), signature.end(), header.begin());
        }
    }

    std::vector<FormatInfo> DdsImporter::formats() const
    {
        return {
            {"DDS", "Image Files", {"dds"}, FileExtension::textrue, resource::AssetType::Texture, 95},
        };
    }

    bool DdsImporter::matches(const std::string& fileType, const DetectionInput& input) const
    {
        if (fileType == "DDS") return matchesSignature(input.header, ddsSig);
        return false;
    }

    void DdsImporter::process(pipeline::ImportContext& context)
    {
        namespace fs = std::filesystem;
        const auto result = textureProcessor.loadDdsFile(context.file, context.fileName, context.location,
                                                         wrapFileProgress<types::TextureProgressCallback>(context));

        if (result == types::TextureImportResult::WroteVfHdr)
            context.outputFiles.push_back(
                {(fs::path(context.location) / (context.fileName + "." + FileExtension::hdr)).string(),
                 resource::AssetType::HDR});
        else if (result == types::TextureImportResult::WroteVfImage)
            context.outputFiles.push_back(
                {(fs::path(context.location) / (context.fileName + "." + FileExtension::textrue)).string(),
                 resource::AssetType::Texture});
        else
            throw std::runtime_error("DDS import failed: " + std::string(context.file.path));
    }
}
