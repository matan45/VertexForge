#include "KtxImporter.hpp"
#include <algorithm>
#include <filesystem>
#include <span>
#include <stdexcept>

namespace import::builtin
{
    namespace
    {
        // 12-byte KTX identifiers (bytes 5-6 = "20" vs "11").
        constexpr unsigned char ktx2Sig[] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
        constexpr unsigned char ktx1Sig[] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};

        bool matchesSignature(std::span<const unsigned char> header, std::span<const unsigned char> signature)
        {
            return header.size() >= signature.size() &&
                   std::equal(signature.begin(), signature.end(), header.begin());
        }
    }

    std::vector<FormatInfo> KtxImporter::formats() const
    {
        // Both map to Texture/vfImage by default; the true per-file type (HDR ->
        // .vfHdr) is set on ImportContext::outputFiles in process().
        return {
            {"KTX2", "Image Files", {"ktx2"}, FileExtension::textrue, resource::AssetType::Texture, 95},
            {"KTX1", "Image Files", {"ktx"},  FileExtension::textrue, resource::AssetType::Texture, 95},
        };
    }

    bool KtxImporter::matches(const std::string& fileType, const DetectionInput& input) const
    {
        if (fileType == "KTX2") return matchesSignature(input.header, ktx2Sig);
        if (fileType == "KTX1") return matchesSignature(input.header, ktx1Sig);
        return false;
    }

    void KtxImporter::process(pipeline::ImportContext& context)
    {
        namespace fs = std::filesystem;
        const auto result = textureProcessor.loadKtxFile(context.file, context.fileName, context.location,
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
            throw std::runtime_error("KTX import failed: " + std::string(context.file.path));
    }
}
