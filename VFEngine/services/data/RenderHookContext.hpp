#pragma once
#include "RenderHookTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

namespace plugin {

    struct RenderHookContext {
        vk::CommandBuffer commandBuffer;
        uint32_t          imageIndex;
        uint32_t          viewportWidth;
        uint32_t          viewportHeight;
        vk::RenderPass    renderPass;
        vk::Device        device;
        glm::mat4         viewMatrix;
        glm::mat4         projectionMatrix;
        glm::vec3         cameraPosition;
        float             nearPlane;
        float             farPlane;
        float             time;
    };

}
