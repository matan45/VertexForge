#pragma once
#include <cstdint>
#include <string>

// Application version - static constants
struct Version
{
    static constexpr uint32_t major = 1;
    static constexpr uint32_t minor = 0;
    static constexpr uint32_t patch = 0;
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
    inline static const std::string animator = "vfAnimator";
    inline static const std::string shader = "glsl";
    inline static const std::string prefab = "vfPrefab";
    inline static const std::string font = "vfFont";
    inline static const std::string project = "vfproj";
    inline static const std::string terrainMaterial = "vfTerrainMat";
    inline static const std::string terrainWeights = "vfTerrainWeights";
    inline static const std::string terrain = "vfTerrain";
    inline static const std::string water = "vfWater";
    inline static const std::string svt = "vfSVT";
    inline static const std::string assetMeta = "vfmeta";
};

namespace importConfig
{
    // Texture compression mode
    enum class TextureCompressionMode
    {
        Uncompressed,
        BC     // BC7 for LDR, BC6H for HDR (desktop standard)
    };

    // Texture compression quality
    enum class TextureCompressionQuality
    {
        Fast,
        Balanced,
        Quality
    };

    // Quality preset for V-HACD convex decomposition
    enum class VHACDPreset
    {
        Fast,      // Quick results, lower quality (~2-4x faster)
        Balanced,  // Good balance of speed and quality (default)
        Quality,   // High accuracy, slower processing
        Custom     // User-defined parameters
    };

    // Mesh-specific import settings for V-HACD convex decomposition
    struct MeshImportConfig
    {
        bool generateConvexDecomposition = false;
        VHACDPreset vhacdPreset = VHACDPreset::Balanced;

        // V-HACD parameters (editable when preset is Custom)
        uint32_t maxConvexHulls = 16;
        uint32_t vhacdResolution = 100000;
        uint32_t maxVerticesPerHull = 64;  // V-HACD default, Jolt limit is 256
        float minVolumePercentError = 1.0f;
        uint32_t maxRecursionDepth = 10;
        bool shrinkWrap = true;  // Snap hull vertices to original mesh surface
    };

    // Audio compression quality for Vorbis encoding
    enum class AudioCompressionQuality
    {
        Low,       // ~80kbps
        Medium,    // ~128kbps
        High,      // ~192kbps
        Lossless   // Raw PCM (no compression)
    };

    // Audio load type strategy
    enum class AudioLoadType
    {
        Auto,              // < 10s = DecompressOnLoad, >= 10s = Streaming
        DecompressOnLoad,  // Decode fully on load (good for SFX)
        Streaming          // Stream from disk (good for music)
    };

    struct AudioImportConfig
    {
        AudioCompressionQuality quality = AudioCompressionQuality::Medium;
        AudioLoadType loadType = AudioLoadType::Auto;
    };

    struct ImportConfig
    {
        bool isImageFlipVertically = false;
        MeshImportConfig meshConfig;
        TextureCompressionMode compressionMode = TextureCompressionMode::BC;
        TextureCompressionQuality compressionQuality = TextureCompressionQuality::Balanced;
        AudioImportConfig audioConfig;
        bool svtEnabled = false;           // Generate .vfSVT for large textures (>= svtMinSize)
        uint32_t svtMinSize = 4096;        // Minimum texture dimension for SVT tiling
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
