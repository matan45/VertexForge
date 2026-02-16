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
        void loadHDRFromStbi(const importConfig::ImportFiles& file, std::string_view fileName,
                             std::string_view location, TextureProgressCallback progressCallback) const;
        void loadHDRFromEXR(const importConfig::ImportFiles& file, std::string_view fileName,
                            std::string_view location, TextureProgressCallback progressCallback) const;

        void saveToFileTextureWithMips(std::string_view fileName, std::string_view location,
                                       const resource::TextureData& textureData) const;
        void saveToFileHDRWithMips(std::string_view fileName, std::string_view location,
                                   const resource::HDRData& hdrData) const;

        void generateMipmaps(resource::TextureData& textureData) const;

        resource::MipLevelData generateMipLevel(const resource::MipLevelData& source) const;

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
