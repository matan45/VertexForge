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
        request.size = sizeof(CameraUBO);

        core::BufferUtilities::createBuffer(request, buffer, allocation, device.getMemoryManager());
        mapped = allocation.mappedPtr;
    }

    void SharedCameraUBO::cleanup()
    {
        mapped = nullptr;

        core::BufferUtilities::destroyBuffer(device.getLogicalDevice(), buffer, allocation, device.getMemoryManager());
    }

    void SharedCameraUBO::update(const glm::mat4& view, const glm::mat4& projection,
                                  const glm::vec3& cameraPos, float time,
                                  float snowAccumulation)
    {
        if (!mapped) return;

        CameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;
        ubo.time = time;
        ubo.snowAccumulation = snowAccumulation;
        math::extractFrustumPlanes(projection * view, ubo.frustumPlanes);

        std::memcpy(mapped, &ubo, sizeof(CameraUBO));
    }
}
