#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
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
        glm::quat localRotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 localScale{1.0f};

        glm::mat4 getLocalOffsetMatrix() const
        {
            glm::mat4 translation = glm::translate(glm::mat4(1.0f), localPosition);
            glm::mat4 rotation = glm::mat4_cast(localRotation);
            glm::mat4 scale = glm::scale(glm::mat4(1.0f), localScale);
            return translation * rotation * scale;
        }
    };
}
