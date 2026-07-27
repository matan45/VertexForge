#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <string>
#include <algorithm>
#include <unordered_map>
#include "../config/Config.hpp"
#include "../animator/SocketTypes.hpp"
#include "../animator/AnimationEventTypes.hpp"
#include "../animator/IKTypes.hpp"

namespace resource
{
    enum class TextureCompressionFormat : uint8_t
    {
        Uncompressed = 0,
        BC7 = 1,
        BC6H = 2
    };

    enum class AudioCompressionFormat : uint8_t
    {
        PCM = 0,
        Vorbis = 1
    };

    enum class AudioLoadType : uint8_t
    {
        DecompressOnLoad = 0,
        Streaming = 1
    };

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
        TEXTURE   = 0,
        MESH      = 1,
        ANIMATION = 2,
        HDR       = 3,
        AUDIO     = 4,
        SCENE     = 5,
        FONT      = 6,
        SKELETON  = 7,
        ANIMATOR  = 8,
        TERRAIN   = 9,
        UNKNOWN   = 255
    };

    struct MipLevelData
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t dataSize = 0; // Explicit byte count (for compressed data; 0 = use width*height*4)
        std::vector<unsigned char> data;
    };

    struct TextureData
    {
        FileType headerFileType = FileType::TEXTURE;
        FileVersion version{};
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t numbersOfChannels = 0;
        uint32_t mipLevels = 1;
        TextureCompressionFormat compressionFormat = TextureCompressionFormat::Uncompressed;
        std::vector<MipLevelData> mipData;

        const std::vector<unsigned char>& textureData() const {
            static std::vector<unsigned char> empty;
            return mipData.empty() ? empty : mipData[0].data;
        }

        void releaseCPUData() {
            for (auto& mip : mipData) {
                mip.data.clear();
                mip.data.shrink_to_fit();
            }
        }
    };

    struct HDRData
    {
        FileType headerFileType = FileType::HDR;
        FileVersion version{};
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t numbersOfChannels = 0;
        uint32_t mipLevels = 1;
        TextureCompressionFormat compressionFormat = TextureCompressionFormat::Uncompressed;
        std::vector<MipLevelData> mipData;

        [[nodiscard]] size_t getDataSize() const
        {
            size_t total = 0;
            for (const auto& mip : mipData)
            {
                total += mip.dataSize > 0 ? mip.dataSize : mip.data.size();
            }
            return total;
        }

        void releaseCPUData()
        {
            for (auto& mip : mipData)
            {
                mip.data.clear();
                mip.data.shrink_to_fit();
            }
        }
    };

    struct Vertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 texCoords;
        glm::ivec4 boneIndices{-1, -1, -1, -1};
        glm::vec4 boneWeights{0.0f, 0.0f, 0.0f, 0.0f};
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
        std::vector<LODLevel> lodLevels;
    };
    

    struct SkeletonBone
    {
        std::string name;
        int32_t parentIndex = -1;
        glm::mat4 offsetMatrix{1.0f};
        glm::mat4 preTransform{1.0f};
    };

    struct PositionKey
    {
        float time = 0.0f;
        glm::vec3 position{0.0f};
    };

    struct RotationKey
    {
        float time = 0.0f;
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    };

    struct ScaleKey
    {
        float time = 0.0f;
        glm::vec3 scale{1.0f};
    };

    struct BoneAnimation
    {
        std::string boneName;
        std::vector<PositionKey> positionKeys;
        std::vector<RotationKey> rotationKeys;
        std::vector<ScaleKey> scalingKeys;
    };

    struct AnimationData
    {
        FileType headerFileType = FileType::ANIMATION;
        FileVersion version{};
        std::string name;
        float duration = 0.0f;
        float ticksPerSecond = 24.0f;
        std::vector<BoneAnimation> channels;
        std::vector<animator::AnimationEvent> events;
    };

    struct AudioData
    {
        FileType headerFileType = FileType::AUDIO;
        FileVersion version{};
        AudioCompressionFormat compressionFormat = AudioCompressionFormat::PCM;
        AudioLoadType loadType = AudioLoadType::DecompressOnLoad;
        uint32_t totalDurationInSeconds = 0;
        uint32_t channels = 0;
        uint32_t sampleRate = 0;
        uint32_t frames = 0;
        std::vector<short> data;               // PCM samples (populated after decode)
        std::vector<uint8_t> compressedData;   // Vorbis compressed data (for serialization)
    };

    struct SkeletonInfo
    {
        std::vector<std::string> boneNames;
        std::vector<glm::mat4> inverseBindPoses;

        bool hasBones() const { return !boneNames.empty(); }
        size_t boneCount() const { return boneNames.size(); }
    };

    struct SkeletonData
    {
        FileType headerFileType = FileType::SKELETON;
        FileVersion version{};
        std::string name;
        std::vector<SkeletonBone> bones;
        std::vector<glm::mat4> bindPoses;
        std::vector<glm::mat4> inverseBindPoses;
        glm::mat4 globalInverseTransform{1.0f};

        std::vector<animator::SocketDefinition> sockets;
        std::vector<animator::ik::IKChainConfig> ikChains;

        bool hasBones() const { return !bones.empty(); }
        size_t boneCount() const { return bones.size(); }

        int32_t getBoneIndex(const std::string& boneName) const
        {
            for (size_t i = 0; i < bones.size(); ++i)
            {
                if (bones[i].name == boneName)
                    return static_cast<int32_t>(i);
            }
            return -1;
        }

        int32_t getSocketIndex(const std::string& socketName) const
        {
            return animator::indexOfSocket(sockets, socketName);
        }

        const animator::SocketDefinition* getSocketByName(const std::string& socketName) const
        {
            int32_t idx = getSocketIndex(socketName);
            return idx >= 0 ? &sockets[idx] : nullptr;
        }
    };

    struct MeshesData
    {
        FileType headerFileType = FileType::MESH;
        FileVersion version{};
        uint32_t numberOfMeshes = 0;
        std::vector<MeshData> meshes;

        bool hasSkinning = false;
        SkeletonData skeleton;
    };

    enum class FontFormatFlags : uint32_t
    {
        NONE            = 0,
        SDF_ENABLED     = 1 << 0,
        KERNING_ENABLED = 1 << 1,
        MULTI_SIZE      = 1 << 2,
        COLOR_EMOJI     = 1 << 3,
        MSDF_ENABLED    = 1 << 4,
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

    enum class FontAtlasFormat : uint32_t
    {
        GRAYSCALE_8   = 0,
        SDF_8         = 1,
        RGBA_32       = 2,
        MTSDF_RGBA_32 = 3,
    };

    [[nodiscard]] inline constexpr uint32_t fontAtlasBytesPerPixel(FontAtlasFormat format) noexcept
    {
        switch (format)
        {
            case FontAtlasFormat::GRAYSCALE_8:
            case FontAtlasFormat::SDF_8:
                return 1;
            case FontAtlasFormat::RGBA_32:
            case FontAtlasFormat::MTSDF_RGBA_32:
                return 4;
            default:
                return 0;
        }
    }

    struct CharacterRange
    {
        uint32_t rangeStart = 0;
        uint32_t rangeEnd = 0;
    };

    struct GlyphData
    {
        uint32_t codepoint = 0;

        float advanceX = 0.0f;
        float advanceY = 0.0f;
        float bearingX = 0.0f;
        float bearingY = 0.0f;
        float glyphWidth = 0.0f;
        float glyphHeight = 0.0f;

        uint32_t atlasX = 0;
        uint32_t atlasY = 0;
        uint32_t atlasWidth = 0;
        uint32_t atlasHeight = 0;

        uint32_t glyphFlags = 0;
    };

    struct KerningPair
    {
        uint32_t leftCodepoint = 0;
        uint32_t rightCodepoint = 0;
        float kerningAmount = 0.0f;
    };

    struct SDFParameters
    {
        float spread = 4.0f;
        uint32_t padding = 4;
        float edgeValue = 0.5f;
        float pxRange = 0.0f;
    };

    static_assert(sizeof(SDFParameters) == 16, "SDFParameters on-disk layout must remain 16 bytes");
    static_assert(offsetof(SDFParameters, spread) == 0);
    static_assert(offsetof(SDFParameters, padding) == 4);
    static_assert(offsetof(SDFParameters, edgeValue) == 8);
    static_assert(offsetof(SDFParameters, pxRange) == 12);

    struct FontMetadata
    {
        std::string fontName;
        std::string fontStyle;
        uint32_t baseFontSize = 32;

        float lineHeight = 0.0f;
        float ascender = 0.0f;
        float descender = 0.0f;
        float underlinePosition = 0.0f;
        float underlineThickness = 0.0f;
    };

    struct FontAtlasData
    {
        uint32_t width = 0;
        uint32_t height = 0;
        FontAtlasFormat format = FontAtlasFormat::GRAYSCALE_8;
        std::vector<unsigned char> pixels;
    };

    struct FontData
    {
        FileType headerFileType = FileType::FONT;
        FileVersion version{};
        FontFormatFlags formatFlags = FontFormatFlags::SDF_ENABLED;

        FontMetadata metadata;
        SDFParameters sdfParams;

        std::vector<CharacterRange> characterRanges;
        std::vector<GlyphData> glyphs;
        std::vector<KerningPair> kerningPairs;

        FontAtlasData atlas;

    private:
        mutable std::unordered_map<uint64_t, float> kerningMap;
        mutable bool kerningMapBuilt = false;

        void buildKerningMap() const
        {
            kerningMap.clear();
            kerningMap.reserve(kerningPairs.size());
            for (const auto& pair : kerningPairs)
            {
                uint64_t key = (static_cast<uint64_t>(pair.leftCodepoint) << 32) |
                               static_cast<uint64_t>(pair.rightCodepoint);
                kerningMap[key] = pair.kerningAmount;
            }
            kerningMapBuilt = true;
        }

    public:
        [[nodiscard]] const GlyphData* findGlyph(uint32_t codepoint) const
        {
            auto it = std::lower_bound(glyphs.begin(), glyphs.end(), codepoint,
                [](const GlyphData& g, uint32_t cp) { return g.codepoint < cp; });

            if (it != glyphs.end() && it->codepoint == codepoint)
                return &(*it);
            return nullptr;
        }

        [[nodiscard]] float getKerning(uint32_t left, uint32_t right) const
        {
            if (!hasFlag(formatFlags, FontFormatFlags::KERNING_ENABLED))
                return 0.0f;

            if (!kerningMapBuilt)
            {
                buildKerningMap();
            }

            uint64_t key = (static_cast<uint64_t>(left) << 32) | static_cast<uint64_t>(right);
            auto it = kerningMap.find(key);
            return (it != kerningMap.end()) ? it->second : 0.0f;
        }

        [[nodiscard]] bool isSDF() const
        {
            return hasFlag(formatFlags, FontFormatFlags::SDF_ENABLED);
        }

        [[nodiscard]] bool isMSDF() const
        {
            return hasFlag(formatFlags, FontFormatFlags::MSDF_ENABLED);
        }
    };

    // VK-1631/VK-1634: normalized-field-space half-width of the SDF anti-aliasing band,
    // handed to both text pipelines as the instance attribute sdfParams.y. The shaders no
    // longer use the magnitude (see resources/shaders/common/text_sdf.glsl): for the legacy
    // single-channel atlas (glyphMode 0) they derive the band from fwidth(), and for MTSDF
    // (glyphMode 2) from the pxRange pushed as a push constant. The value now only signals
    // SDF (> 0) vs non-SDF (== 0) - and it still drives the CPU-side font-preview bake for
    // the legacy atlas (utilities/resource/FontAtlasPreview.cpp).
    [[nodiscard]] inline float sdfSmoothWidth(const FontData& font) noexcept
    {
        if (!font.isSDF())
        {
            return 0.0f;
        }
        if (font.isMSDF())
        {
            return font.sdfParams.pxRange > 0.0f ? 0.5f / font.sdfParams.pxRange : 0.1f;
        }
        return font.sdfParams.spread > 0.0f ? 0.5f / font.sdfParams.spread : 0.1f;
    }

}
