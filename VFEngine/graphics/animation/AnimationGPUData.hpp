#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace animation
{
    // GPU-side structures matching the compute shader layout

    struct GPUAnimEvalRequest
    {
        uint32_t animDataIndex;
        uint32_t skeletonIndex;
        uint32_t boneCount;
        uint32_t outputBoneOffset;
        float timeInTicks;
        float blendWeight;
        uint32_t secondAnimIndex;
        float secondTime;
    };
    static_assert(sizeof(GPUAnimEvalRequest) == 32);

    struct GPUBoneInfo
    {
        int32_t parentIndex;
        float _pad[3];
        glm::mat4 preTransform;
        glm::mat4 offsetMatrix;
        glm::mat4 inverseBindPose;
    };
    static_assert(sizeof(GPUBoneInfo) == 208);

    struct GPUPositionKey
    {
        float time;
        glm::vec3 position;
    };
    static_assert(sizeof(GPUPositionKey) == 16);

    struct GPURotationKey
    {
        float time;
        glm::vec4 rotation;  // quaternion (x, y, z, w)
    };
    static_assert(sizeof(GPURotationKey) == 20);

    struct GPUScaleKey
    {
        float time;
        glm::vec3 scale;
    };
    static_assert(sizeof(GPUScaleKey) == 16);

    struct GPUChannelHeader
    {
        uint32_t positionKeyOffset;
        uint32_t positionKeyCount;
        uint32_t rotationKeyOffset;
        uint32_t rotationKeyCount;
        uint32_t scaleKeyOffset;
        uint32_t scaleKeyCount;
        uint32_t boneIndex;
        uint32_t _pad;
    };
    static_assert(sizeof(GPUChannelHeader) == 32);

    struct GPUAnimClipHeader
    {
        float duration;
        float ticksPerSecond;
        uint32_t channelCount;
        uint32_t channelHeaderOffset;
        glm::mat4 globalInverseTransform;
    };
    static_assert(sizeof(GPUAnimClipHeader) == 80);

    // Aggregated GPU animation data for upload
    struct AnimationGPUUploadData
    {
        std::vector<GPUAnimClipHeader> clipHeaders;
        std::vector<GPUChannelHeader> channelHeaders;
        std::vector<GPUPositionKey> positionKeys;
        std::vector<GPURotationKey> rotationKeys;
        std::vector<GPUScaleKey> scaleKeys;
        std::vector<GPUBoneInfo> skeletonBones;

        void clear()
        {
            clipHeaders.clear();
            channelHeaders.clear();
            positionKeys.clear();
            rotationKeys.clear();
            scaleKeys.clear();
            skeletonBones.clear();
        }
    };
}
