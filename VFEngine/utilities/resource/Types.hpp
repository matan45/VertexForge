#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>
#include <string>
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
    
}
