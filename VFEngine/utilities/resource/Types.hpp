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
        UNKNOWN
    };

    enum class FileType : uint8_t
    {
        SHADER,
        TEXTURE,
        MESH,
        ANIMATION,
        HDR,
        AUDIO,
        UNKNOWN
    };

    struct TextureData
    {
        FileType headerFileType = FileType::TEXTURE;
        Version version{}; // Default initialize
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t numbersOfChannels = 0;
        std::vector<unsigned char> textureData;
    };

    struct HDRData
    {
        FileType headerFileType = FileType::HDR;
        Version version{}; // Default initialize
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t numbersOfChannels = 0;
        std::vector<float> textureData;
    };

    struct Vertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 texCoords;
    };

    struct MeshData
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    struct MeshesData
    {
        FileType headerFileType = FileType::MESH;
        Version version{}; // Default initialize
        uint32_t numberOfMeshes = 0;
        std::vector<MeshData> meshes;
    };

    struct Bone
    {
        std::string name; // Name of the bone
        glm::mat4 offsetMatrix; // Inverse Bind Pose Matrix (Bone's offset matrix)
        std::vector<std::pair<uint32_t, float>> weights; // Vertex index and weight pairs
    };

    struct Keyframe
    {
        float time;
        glm::vec3 position;
        glm::quat rotation; // Quaternions (w, x, y, z)
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
        Version version{}; // Default initialize
        float duration = 0.0f;
        float ticksPerSecond = 0.0f;
        uint32_t numBones = 0;
        std::vector<BoneAnimation> boneAnimations;
        std::vector<Bone> bones;
    };

    struct AudioData
    {
        FileType headerFileType = FileType::AUDIO;
        Version version{}; // Default initialize
        uint32_t totalDurationInSeconds = 0;
        uint32_t channels = 0;
        uint32_t sampleRate = 0;
        uint32_t frames = 0;
        std::vector<short> data;
    };
}
