#pragma once
#include <string>
#include "Types.hpp"

namespace resource
{
    class TextureResource
    {
    private:
        inline static const size_t chunkSize = 1024 * 1024;

    public:
        static TextureData loadTexture(std::string_view path);
        static HDRData loadHDR(std::string_view path);
    };

    class HDRReader
    {
    public:
        static void readHDR(std::ifstream& file, int width, int height, int channels, std::vector<float>& pixels);
    };

    struct TGAImage {
        int width;
        int height;
        int channels;
        std::vector<uint8_t> pixelData;
    };
    
    class TGAReader
    {
    public:
        static void readTGA(std::ifstream& file, int width, int height,
                            std::vector<unsigned char>& pixelData);
    };
}
