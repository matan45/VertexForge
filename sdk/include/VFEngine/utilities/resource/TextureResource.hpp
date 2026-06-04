#pragma once
#include <string>
#include <istream>
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
        static void readHDR(std::istream& file, int width, int height, int channels, std::vector<float>& pixels);
    };

    class TGAReader
    {
    public:
        static void readTGA(std::istream& file, int width, int height,
                            std::vector<unsigned char>& pixelData);
    };
}
