#pragma once
#include <cstdint>
#include <string>

// Application version - static constants
struct Version
{
    static constexpr uint32_t major = 0;
    static constexpr uint32_t minor = 0;
    static constexpr uint32_t patch = 3;
};

// File version - instance members for storing version info read from files
struct FileVersion
{
    uint32_t major = Version::major;
    uint32_t minor = Version::minor;
    uint32_t patch = Version::patch;
};

struct FileExtension
{
    inline static const std::string textrue = "vfImage";
    inline static const std::string hdr = "vfHdr";
    inline static const std::string audio = "vfAudio";
    inline static const std::string mesh = "vfMesh";
    inline static const std::string animation = "vfAnim";
    inline static const std::string shader = "glsl";
    inline static const std::string prefab = "vfPrefab";
    inline static const std::string font = "vfFont";
};

namespace importConfig
{
    // Mesh-specific import settings for V-HACD convex decomposition
    struct MeshImportConfig
    {
        bool generateConvexDecomposition = false;
        uint32_t maxConvexHulls = 16;
        uint32_t vhacdResolution = 100000;
        uint32_t maxVerticesPerHull = 32;
        float minVolumePercentError = 1.0f;
    };

    struct ImportConfig
    {
        bool isImageFlipVertically = false;
        MeshImportConfig meshConfig;
    };

    struct ImportFiles
    {
        std::string path; // Must own the string - views dangle when source is destroyed
        ImportConfig config;

        explicit ImportFiles(std::string_view path, const ImportConfig& config) :
            path{path}, config{config}
        {
        }
    };
}
