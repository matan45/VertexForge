// PSDImport — sample plugin asset importer (API v12).
// Imports Photoshop .psd files (composited image, via stb_image) into the
// engine's standard uncompressed .vfImage, so the output gets Content Browser,
// runtime loading and GameExport support with zero engine changes.
#include "api/IPlugin.hpp"
#include "api/PluginContext.hpp"
#include "api/PluginExport.hpp"
#include "registry/AssetImporter.hpp"
#include "resource/EndianUtils.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PSD
#include "stb_image.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <variant>

namespace
{
    class PsdImporter : public import::AssetImporter
    {
    public:
        std::vector<import::FormatInfo> formats() const override
        {
            return {
                {"PSD", "Photoshop Files", {"psd"}, FileExtension::textrue,
                 resource::AssetType::Texture, 100},
            };
        }

        bool matches(const std::string& fileType, const import::DetectionInput& input) const override
        {
            return fileType == "PSD" && input.header.size() >= 4 &&
                   input.header[0] == '8' && input.header[1] == 'B' &&
                   input.header[2] == 'P' && input.header[3] == 'S';
        }

        std::vector<import::ImportOptionDesc> options() const override
        {
            import::ImportOptionDesc flip;
            flip.key = "flipVertically";
            flip.label = "Flip Vertically";
            flip.tooltip = "Flip the composited PSD image vertically on import";
            flip.type = import::ImportOptionDesc::Type::Bool;
            flip.defaultValue = false;
            return {flip};
        }

        void process(pipeline::ImportContext& context) override
        {
            int width = 0, height = 0, channels = 0;
            stbi_uc* pixels = stbi_load(context.file.path.c_str(), &width, &height, &channels, 4);
            if (!pixels)
                throw std::runtime_error("Failed to decode PSD: " + context.file.path);

            std::vector<unsigned char> rgba(pixels, pixels + static_cast<size_t>(width) * height * 4);
            stbi_image_free(pixels);

            if (flipRequested(context))
                flipVertically(rgba, width, height);

            reportProgress(context, 0.5f);

            const std::string outputPath = (std::filesystem::path(context.location) /
                                            (context.fileName + "." + FileExtension::textrue)).string();
            writeVfImage(outputPath, static_cast<uint32_t>(width), static_cast<uint32_t>(height), rgba);

            reportProgress(context, 1.0f);
        }

    private:
        static bool flipRequested(const pipeline::ImportContext& context)
        {
            const auto& options = context.file.config.customOptions;
            auto it = options.find("flipVertically");
            return it != options.end() && std::holds_alternative<bool>(it->second) &&
                   std::get<bool>(it->second);
        }

        static void flipVertically(std::vector<unsigned char>& rgba, int width, int height)
        {
            const size_t rowBytes = static_cast<size_t>(width) * 4;
            std::vector<unsigned char> row(rowBytes);
            for (int y = 0; y < height / 2; ++y)
            {
                unsigned char* top = rgba.data() + y * rowBytes;
                unsigned char* bottom = rgba.data() + (height - 1 - y) * rowBytes;
                std::memcpy(row.data(), top, rowBytes);
                std::memcpy(top, bottom, rowBytes);
                std::memcpy(bottom, row.data(), rowBytes);
            }
        }

        static void reportProgress(pipeline::ImportContext& context, float progress)
        {
            if (context.progressCallback)
            {
                context.progressCallback(context.fileName, context.fileIndex + 1,
                                         context.totalFiles, progress);
            }
        }

        // Uncompressed single-mip .vfImage, same layout as the engine's
        // procedural::VFImageWriter (which is not part of the SDK — plugins
        // are self-contained): version header, then BGRA pixel data.
        static void writeVfImage(const std::string& outputPath, uint32_t width, uint32_t height,
                                 const std::vector<unsigned char>& rgba)
        {
            std::ofstream outFile(outputPath, std::ios::binary);
            if (!outFile)
                throw std::runtime_error("Failed to open output: " + outputPath);

            constexpr uint8_t fileType = 0;          // TEXTURE
            constexpr uint8_t compressionFormat = 0; // Uncompressed
            constexpr uint32_t channels = 4;
            constexpr uint32_t mipLevels = 1;

            resource::endian::writeLE<uint8_t>(outFile, fileType);
            resource::endian::writeLE<uint32_t>(outFile, Version::major);
            resource::endian::writeLE<uint32_t>(outFile, Version::minor);
            resource::endian::writeLE<uint32_t>(outFile, Version::patch);
            resource::endian::writeLE<uint32_t>(outFile, width);
            resource::endian::writeLE<uint32_t>(outFile, height);
            resource::endian::writeLE<uint32_t>(outFile, channels);
            resource::endian::writeLE<uint32_t>(outFile, mipLevels);
            resource::endian::writeLE<uint8_t>(outFile, compressionFormat);

            resource::endian::writeLE<uint32_t>(outFile, width);
            resource::endian::writeLE<uint32_t>(outFile, height);
            resource::endian::writeLE<uint32_t>(outFile, width * height * channels);

            for (size_t i = 0; i < rgba.size(); i += 4)
            {
                unsigned char bgra[4] = {rgba[i + 2], rgba[i + 1], rgba[i], rgba[i + 3]};
                outFile.write(reinterpret_cast<const char*>(bgra), 4);
            }

            if (!outFile.good())
                throw std::runtime_error("Failed to write output: " + outputPath);
        }
    };

    class PSDImportPlugin : public plugin::IPlugin
    {
    public:
        plugin::PluginInfo getInfo() const override
        {
            plugin::PluginInfo info;
            info.name = "PSDImport";
            info.author = "VertexForge";
            info.description = "Imports Photoshop .psd files as .vfImage textures (sample asset importer)";
            return info;
        }

        bool onInitialize(plugin::PluginContext* context) override
        {
            context->registerAssetImporter(std::make_unique<PsdImporter>());
            context->logInfo("PSDImport: .psd importer registered");
            return true;
        }

        void onShutdown() override
        {
            // The importer is unregistered engine-side (after draining
            // in-flight imports) before this DLL unloads.
        }
    };
}

VF_IMPLEMENT_PLUGIN(PSDImportPlugin)
