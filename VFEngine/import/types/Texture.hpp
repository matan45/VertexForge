#pragma once
#include <string>
#include <functional>
#include "config/Config.hpp"
#include "resource/Types.hpp"

namespace types
{
    using TextureProgressCallback = std::function<void(float progress)>;

    class Texture
    {
    public:
        void loadTextureFile(const importConfig::ImportFiles& file, std::string_view fileName,
                             std::string_view location, TextureProgressCallback progressCallback = nullptr);
        void loadHDRFile(const importConfig::ImportFiles& file, std::string_view fileName,
                         std::string_view location, TextureProgressCallback progressCallback = nullptr) const;

    private:
        // Save functions with mipmap support (v0.0.3 format)
        void saveToFileTextureWithMips(std::string_view fileName, std::string_view location,
                                       const resource::TextureData& textureData) const;
        void saveToFileHDRWithMips(std::string_view fileName, std::string_view location,
                                   const resource::HDRData& hdrData) const;

        // Mipmap generation using box filter (2x2 averaging)
        void generateMipmaps(resource::TextureData& textureData) const;
        void generateMipmapsHDR(resource::HDRData& hdrData) const;

        // Generate a single mip level from parent level using box filter
        resource::MipLevelData generateMipLevel(const resource::MipLevelData& source) const;
        resource::MipLevelDataHDR generateMipLevelHDR(const resource::MipLevelDataHDR& source) const;

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
