#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
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

        glm::mat4 getLocalOffsetMatrix() const
        {
            return glm::translate(glm::mat4(1.0f), localPosition);
        }
    };
}
