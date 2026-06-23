#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace animator
{
    struct SocketDefinition
    {
        std::string name;
        std::string targetBoneName;
        int32_t boneIndex = -1;
        glm::vec3 localPosition{0.0f};
        glm::quat localRotation{1.0f, 0.0f, 0.0f, 0.0f}; // identity (w,x,y,z)

        glm::mat4 getLocalOffsetMatrix() const
        {
            return glm::translate(glm::mat4(1.0f), localPosition) * glm::mat4_cast(localRotation);
        }
    };

    // Linear name scan over a contiguous range of sockets; -1 if absent.
    // Single home for the three previously byte-identical scans (SocketAttachmentUpdater,
    // SocketAdapter, SkeletonData::getSocketIndex).
    inline int32_t indexOfSocket(std::span<const SocketDefinition> sockets, std::string_view name)
    {
        for (size_t i = 0; i < sockets.size(); ++i)
        {
            if (sockets[i].name == name)
                return static_cast<int32_t>(i);
        }
        return -1;
    }
}
