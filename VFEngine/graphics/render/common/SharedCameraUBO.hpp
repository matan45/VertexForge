#pragma once

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include "CameraTypes.hpp"

namespace core
{
    class Device;
}

namespace render::common
{
    class SharedCameraUBO
    {
    private:
        core::Device& device;
        vk::Buffer buffer;
        vk::DeviceMemory memory;
        void* mapped = nullptr;

    public:
        explicit SharedCameraUBO(core::Device& device);
        ~SharedCameraUBO();

        SharedCameraUBO(const SharedCameraUBO&) = delete;
        SharedCameraUBO& operator=(const SharedCameraUBO&) = delete;

        void init();
        void cleanup();

        void update(const glm::mat4& view, const glm::mat4& projection,
                    const glm::vec3& cameraPos, float time);

        vk::Buffer getBuffer() const { return buffer; }
    };
}
