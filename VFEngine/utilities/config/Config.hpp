#pragma once
#include <cstdint>
#include <string>

struct Version
{
    static constexpr  uint32_t major = 0;
    static constexpr uint32_t minor = 0;
    static constexpr uint32_t patch = 2;  // v0.0.2: Added submesh names to vfMesh format
};

struct FileExtension
{
    inline static const std::string textrue = "vfImage";
    inline static const std::string hdr = "vfHdr";
    inline static const std::string audio = "vfAudio";
    inline static const std::string mesh = "vfMesh";
    inline static const std::string animation = "vfAnim";
    inline static const std::string shader = "glsl";
};

namespace importConfig
{
    struct ImportConfig
    {
        bool isImageFlipVertically = false;
    };

    struct ImportFiles
    {
        std::string path;  // Must own the string - views dangle when source is destroyed
        ImportConfig config;

        explicit ImportFiles(std::string_view path, const ImportConfig& config):
            path{path}, config{config}
        {
        }
    };
}
