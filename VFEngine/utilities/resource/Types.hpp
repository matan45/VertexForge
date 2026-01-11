#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>
#include <string>
#include <algorithm>
#include "../config/Config.hpp"

namespace resource
{
    enum class ShaderType :uint8_t
    {
        VERTEX,
        FRAGMENT,
        COMPUTE,
        GEOMETRY,
        TESS_CONTROL,
        TESS_EVALUATION,
        MESH,
        TASK,
        UNKNOWN
    };

    enum class FileType : uint8_t
    {
        TEXTURE,
        MESH,
        ANIMATION,
        HDR,
        AUDIO,
        SCENE,
        FONT,
        UNKNOWN
    };

    // Mip level data - stores pixel data for a single mipmap level
    struct MipLevelData
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<unsigned char> data;  // RGBA pixel data for this mip level
    };

    // HDR mip level data - stores float pixel data for a single mipmap level
    struct MipLevelDataHDR
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<float> data;  // RGBA32F pixel data for this mip level
    };
    
    inline uint32_t calculateMipLevels(uint32_t width, uint32_t height)
    {
        uint32_t levels = 1;
        while (width > 1 || height > 1)
        {
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
            levels++;
        }
        return levels;
    }

    struct TextureData
    {
        FileType headerFileType = FileType::TEXTURE;
        FileVersion version{};
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t numbersOfChannels = 0;
        uint32_t mipLevels = 1;                  // Number of mip levels (1 = no mipmaps)
        std::vector<MipLevelData> mipData;       // Mip chain (mipData[0] = base level)
        
        const std::vector<unsigned char>& textureData() const {
            static std::vector<unsigned char> empty;
            return mipData.empty() ? empty : mipData[0].data;
        }
        
        void setTextureData(std::vector<unsigned char>&& data) {
            mipData.clear();
            mipData.push_back({width, height, std::move(data)});
            mipLevels = 1;
        }
        
        void releaseCPUData() {
            for (auto& mip : mipData) {
                mip.data.clear();
                mip.data.shrink_to_fit();
            }
        }
        
        bool hasCPUData() const {
            return !mipData.empty() && !mipData[0].data.empty();
        }
        
        size_t getCPUMemoryUsage() const {
            size_t total = 0;
            for (const auto& mip : mipData) {
                total += mip.data.capacity();
            }
            return total;
        }
    };

    struct HDRData
    {
        FileType headerFileType = FileType::HDR;
        FileVersion version{};
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t numbersOfChannels = 0;
        std::vector<float> pixels;

        [[nodiscard]] size_t getDataSize() const
        {
            return pixels.size() * sizeof(float);
        }

        void releaseCPUData()
        {
            pixels.clear();
            pixels.shrink_to_fit();
        }
    };

    struct Vertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 texCoords;
    };
    
    struct LODLevel
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

   
    constexpr uint32_t LOD_LEVEL_COUNT = 4;

    struct MeshData
    {
        std::string name;
        std::vector<LODLevel> lodLevels; // 4 LOD levels (LOD0=100%, LOD1=50%, LOD2=25%, LOD3=12.5%)
        
        const std::vector<Vertex>& vertices() const {
            static std::vector<Vertex> empty;
            return lodLevels.empty() ? empty : lodLevels[0].vertices;
        }
        const std::vector<uint32_t>& indices() const {
            static std::vector<uint32_t> empty;
            return lodLevels.empty() ? empty : lodLevels[0].indices;
        }
    };
    

    struct Bone
    {
        std::string name;
        glm::mat4 offsetMatrix; // Inverse Bind Pose Matrix (Bone's offset matrix)
        std::vector<std::pair<uint32_t, float>> weights;
    };

    struct Keyframe
    {
        float time;
        glm::vec3 position;
        glm::quat rotation;
        glm::vec3 scale;
    };

    struct BoneAnimation
    {
        std::string boneName;
        std::vector<Keyframe> positionKeys;
        std::vector<Keyframe> rotationKeys;
        std::vector<Keyframe> scalingKeys;
    };

    struct AnimationData
    {
        FileType headerFileType = FileType::ANIMATION;
        FileVersion version{};
        float duration = 0.0f;
        float ticksPerSecond = 0.0f;
        uint32_t numBones = 0;
        std::vector<BoneAnimation> boneAnimations;
        std::vector<Bone> bones;
    };

    struct AudioData
    {
        FileType headerFileType = FileType::AUDIO;
        FileVersion version{};
        uint32_t totalDurationInSeconds = 0;
        uint32_t channels = 0;
        uint32_t sampleRate = 0;
        uint32_t frames = 0;
        std::vector<short> data;
    };

    struct MeshesData
    {
        FileType headerFileType = FileType::MESH;
        FileVersion version{};
        uint32_t numberOfMeshes = 0;
        std::vector<MeshData> meshes;
    };

    // ============================================================================
    // Font Asset Format (.vfFont)
    // ============================================================================

    // Font format feature flags (bit field)
    enum class FontFormatFlags : uint32_t
    {
        NONE            = 0,
        SDF_ENABLED     = 1 << 0,   // Atlas uses Signed Distance Field
        KERNING_ENABLED = 1 << 1,   // Kerning table present
        MULTI_SIZE      = 1 << 2,   // Reserved: multiple rasterized sizes (v2.0)
        COLOR_EMOJI     = 1 << 3,   // Reserved: color emoji support (v2.0)
        MSDF_ENABLED    = 1 << 4,   // Reserved: Multi-channel SDF (v2.0)
    };

    inline FontFormatFlags operator|(FontFormatFlags a, FontFormatFlags b)
    {
        return static_cast<FontFormatFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline FontFormatFlags operator&(FontFormatFlags a, FontFormatFlags b)
    {
        return static_cast<FontFormatFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    inline bool hasFlag(FontFormatFlags flags, FontFormatFlags flag)
    {
        return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
    }

    // Atlas pixel format
    enum class FontAtlasFormat : uint32_t
    {
        GRAYSCALE_8 = 0,   // 1 byte per pixel (standard bitmap)
        SDF_8       = 1,   // 1 byte per pixel (SDF encoded)
        RGBA_32     = 2,   // 4 bytes per pixel (color emoji)
    };

    // Unicode character range
    struct CharacterRange
    {
        uint32_t rangeStart = 0;
        uint32_t rangeEnd = 0;
    };

    // Glyph metrics and atlas positioning (48 bytes)
    struct GlyphData
    {
        uint32_t codepoint = 0;      // Unicode codepoint

        // Metrics (in pixels at base font size)
        float advanceX = 0.0f;       // Horizontal advance to next character
        float advanceY = 0.0f;       // Vertical advance (usually 0 for horizontal text)
        float bearingX = 0.0f;       // Left side bearing
        float bearingY = 0.0f;       // Top side bearing (from baseline)
        float glyphWidth = 0.0f;     // Glyph bounding box width
        float glyphHeight = 0.0f;    // Glyph bounding box height

        // Atlas texture coordinates (in pixels)
        uint32_t atlasX = 0;
        uint32_t atlasY = 0;
        uint32_t atlasWidth = 0;
        uint32_t atlasHeight = 0;

        uint32_t reserved = 0;       // Future: kerning table index
    };

    // Kerning pair
    struct KerningPair
    {
        uint32_t leftCodepoint = 0;
        uint32_t rightCodepoint = 0;
        float kerningAmount = 0.0f;  // Horizontal adjustment
    };

    // SDF-specific parameters
    struct SDFParameters
    {
        float spread = 4.0f;         // SDF spread radius in pixels
        uint32_t padding = 4;        // Padding around glyphs for SDF
        float edgeValue = 0.5f;      // Edge threshold (0.5 = glyph edge)
        uint32_t reserved = 0;
    };

    // Font metadata
    struct FontMetadata
    {
        std::string fontName;        // Font family name (e.g., "Roboto")
        std::string fontStyle;       // Style variant (e.g., "Regular", "Bold", "Italic")
        uint32_t baseFontSize = 32;  // Rasterization size in pixels

        // Vertical metrics
        float lineHeight = 0.0f;     // Line height (ascender - descender + line gap)
        float ascender = 0.0f;       // Maximum ascent above baseline
        float descender = 0.0f;      // Maximum descent below baseline (negative)
        float underlinePosition = 0.0f;
        float underlineThickness = 0.0f;
    };

    // Atlas texture data
    struct FontAtlasData
    {
        uint32_t width = 0;
        uint32_t height = 0;
        FontAtlasFormat format = FontAtlasFormat::GRAYSCALE_8;
        std::vector<unsigned char> pixels;

        [[nodiscard]] size_t getDataSize() const
        {
            size_t bytesPerPixel = 1;
            if (format == FontAtlasFormat::RGBA_32) bytesPerPixel = 4;
            return static_cast<size_t>(width) * height * bytesPerPixel;
        }

        void releaseCPUData()
        {
            pixels.clear();
            pixels.shrink_to_fit();
        }

        [[nodiscard]] bool hasCPUData() const
        {
            return !pixels.empty();
        }
    };

    // Complete font asset data structure
    struct FontData
    {
        FileType headerFileType = FileType::FONT;
        FileVersion version{};
        FontFormatFlags formatFlags = FontFormatFlags::SDF_ENABLED;

        FontMetadata metadata;
        SDFParameters sdfParams;     // Valid only if SDF_ENABLED flag set

        std::vector<CharacterRange> characterRanges;
        std::vector<GlyphData> glyphs;
        std::vector<KerningPair> kerningPairs;  // Valid only if KERNING_ENABLED flag set

        FontAtlasData atlas;

        // Find glyph by codepoint (glyphs should be sorted by codepoint)
        [[nodiscard]] const GlyphData* findGlyph(uint32_t codepoint) const
        {
            auto it = std::lower_bound(glyphs.begin(), glyphs.end(), codepoint,
                [](const GlyphData& g, uint32_t cp) { return g.codepoint < cp; });

            if (it != glyphs.end() && it->codepoint == codepoint)
                return &(*it);
            return nullptr;
        }

        // Get kerning adjustment between two glyphs
        [[nodiscard]] float getKerning(uint32_t left, uint32_t right) const
        {
            if (!hasFlag(formatFlags, FontFormatFlags::KERNING_ENABLED))
                return 0.0f;

            for (const auto& pair : kerningPairs)
            {
                if (pair.leftCodepoint == left && pair.rightCodepoint == right)
                    return pair.kerningAmount;
            }
            return 0.0f;
        }

        [[nodiscard]] bool isSDF() const
        {
            return hasFlag(formatFlags, FontFormatFlags::SDF_ENABLED);
        }

        void releaseCPUData()
        {
            atlas.releaseCPUData();
        }
    };

}
