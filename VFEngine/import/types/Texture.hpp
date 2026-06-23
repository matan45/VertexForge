#pragma once
#include <string>
#include <functional>
#include "config/Config.hpp"
#include "resource/Types.hpp"
#include "../ImportExport.hpp"

namespace types
{
    using TextureProgressCallback = std::function<void(float progress)>;

    class VF_IMPORT_API Texture
    {
    public:
        void loadTextureFile(const importConfig::ImportFiles& file, std::string_view fileName,
                             std::string_view location, TextureProgressCallback progressCallback = nullptr);
        void loadHDRFile(const importConfig::ImportFiles& file, std::string_view fileName,
                         std::string_view location, TextureProgressCallback progressCallback = nullptr) const;

        // Decodes an embedded texture blob and writes it as a .vfImage using the
        // standard mip + compression pipeline (honoring the import config's
        // compression mode/quality). isCompressed (Assimp aiTexture::mHeight == 0):
        // `data` is a `byteLength`-byte encoded file (png/jpg/...). Otherwise
        // `data` is width*height BGRA8 texels. Returns true if a .vfImage was written.
        bool saveEmbeddedTexture(std::string_view fileName, std::string_view location,
                                 const unsigned char* data, size_t byteLength,
                                 bool isCompressed, uint32_t width, uint32_t height,
                                 const importConfig::ImportConfig& config) const;

    private:
        // Builds a .vfImage from RGBA8 pixel data via mips + compression. Shared by
        // loadTextureFile and saveEmbeddedTexture.
        void writeRGBAAsVfImage(std::string_view fileName, std::string_view location,
                                std::vector<unsigned char> rgba, uint32_t width, uint32_t height,
                                uint32_t numChannels, importConfig::TextureCompressionMode mode,
                                importConfig::TextureCompressionQuality quality) const;

        void loadHDRFromStbi(const importConfig::ImportFiles& file, std::string_view fileName,
                             std::string_view location, TextureProgressCallback progressCallback) const;
        void loadHDRFromEXR(const importConfig::ImportFiles& file, std::string_view fileName,
                            std::string_view location, TextureProgressCallback progressCallback) const;

        void saveToFileTextureWithMips(std::string_view fileName, std::string_view location,
                                       const resource::TextureData& textureData) const;
        void saveToFileHDRWithMips(std::string_view fileName, std::string_view location,
                                   const resource::HDRData& hdrData) const;

        void generateMipmaps(resource::TextureData& textureData) const;
        void generateHDRMipmaps(std::vector<float>& basePixels, uint32_t width, uint32_t height,
                                resource::HDRData& hdrData) const;

        resource::MipLevelData generateMipLevel(const resource::MipLevelData& source) const;

        void compressTextureMips(resource::TextureData& textureData,
                                 importConfig::TextureCompressionMode mode,
                                 importConfig::TextureCompressionQuality quality) const;
        void compressHDRMips(resource::HDRData& hdrData,
                             importConfig::TextureCompressionMode mode,
                             importConfig::TextureCompressionQuality quality) const;

        void convertTo4Channels(unsigned char* inputData, int width, int height, int inputChannels,
            std::vector<unsigned char>& outputData);

        std::vector<float> convertToRGBA32F(const float* data, int width, int height, int channels) const;
        void flipImageVertically(float* imageData, int width, int height) const;
    };

    class TGAWriter
    {
        public:
        static void writeTGA(std::ofstream& file, const std::vector<unsigned char>& pixelData);
    };
}
