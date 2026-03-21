#include "SharedCameraUBO.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "math/Frustum.hpp"
#include <cstring>

namespace render::common
{
    SharedCameraUBO::SharedCameraUBO(core::Device& device)
        : device(device)
    {
    }

    SharedCameraUBO::~SharedCameraUBO()
    {
        cleanup();
    }

    void SharedCameraUBO::init()
    {
        core::BufferInfoRequest request(device.getLogicalDevice(), device.getPhysicalDevice());
        request.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                             vk::MemoryPropertyFlagBits::eHostCoherent;
        request.size = sizeof(mesh::CameraUBO);

        core::BufferUtilities::createBuffer(request, buffer, memory);
        mapped = device.getLogicalDevice().mapMemory(memory, 0, sizeof(mesh::CameraUBO));
    }

    void SharedCameraUBO::cleanup()
    {
        if (mapped)
        {
            device.getLogicalDevice().unmapMemory(memory);
            mapped = nullptr;
        }

        core::BufferUtilities::destroyBuffer(device.getLogicalDevice(), buffer, memory);
    }

    void SharedCameraUBO::update(const glm::mat4& view, const glm::mat4& projection,
                                  const glm::vec3& cameraPos, float time)
    {
        if (!mapped) return;

        mesh::CameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;
        ubo.time = time;
        math::extractFrustumPlanes(projection * view, ubo.frustumPlanes);

        std::memcpy(mapped, &ubo, sizeof(mesh::CameraUBO));
    }
}
