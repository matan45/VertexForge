#include "HdrImporter.hpp"
#include "../../types/BRDFLUTExporter.hpp"
#include "resource/PathResolver.hpp"
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <ranges>

namespace import::builtin
{
    namespace
    {
        constexpr unsigned char exrSig[] = {0x76, 0x2F, 0x31, 0x01};

        // EXR signature is search-based (not a prefix match).
        bool isEXR(std::span<const unsigned char> header)
        {
            if (header.empty()) return false;
            auto result = std::ranges::search(header, exrSig);
            return !result.empty();
        }

        bool isHDR(std::span<const unsigned char> header)
        {
            const std::string hdrSignature1 = "#?RADIANCE";
            const std::string hdrSignature2 = "#?RGBE";

            if (header.size() < hdrSignature1.size()) return false;

            return std::equal(header.begin(), header.begin() + hdrSignature1.size(), hdrSignature1.begin()) ||
                   (header.size() >= hdrSignature2.size() &&
                    std::equal(header.begin(), header.begin() + hdrSignature2.size(), hdrSignature2.begin()));
        }
    }

    std::vector<FormatInfo> HdrImporter::formats() const
    {
        return {
            {"EXR", "Hdr Files", {"exr"}, FileExtension::hdr, resource::AssetType::HDR, 90},
            {"HDR", "Hdr Files", {"hdr"}, FileExtension::hdr, resource::AssetType::HDR, 80},
        };
    }

    bool HdrImporter::matches(const std::string& fileType, const DetectionInput& input) const
    {
        if (fileType == "EXR") return isEXR(input.header);
        if (fileType == "HDR") return isHDR(input.header);
        return false;
    }

    void HdrImporter::process(pipeline::ImportContext& context)
    {
        textureProcessor.loadHDRFile(context.file, context.fileName, context.location,
                                     wrapFileProgress<types::TextureProgressCallback>(context));

        // First HDR import generates the shared BRDF LUT if it doesn't exist yet.
        static std::once_flag brdfLutFlag;
        std::call_once(brdfLutFlag, []()
        {
            const std::string lutPath = resource::PathResolver::resolveEnginePath("../../resources/ibl/brdf_lut.vfImage");
            if (!std::filesystem::exists(lutPath))
            {
                types::BRDFLUTExporter::generateAndSave(lutPath);
            }
        });
    }
}
