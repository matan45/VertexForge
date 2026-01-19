#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>
#include <string>
#include <algorithm>
#include <unordered_map>
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
        SKELETON,
        ANIMATOR,
        UNKNOWN
    };

    struct MipLevelData
    {
        uint32_t width = 0;
        uint32_t height = 0;
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
        std::vector<float> pixels;

        [[nodiscard]] size_t getDataSize() const
        {
            return pixels.size() * sizeof(float);
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
        GRAYSCALE_8 = 0,
        SDF_8       = 1,
        RGBA_32     = 2,
    };

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

        uint32_t reserved = 0;
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
        uint32_t reserved = 0;
    };

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
    };

}
