#include "GPUDrivenCameraBuffer.hpp"
#include "scene/IndirectBatchManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/BufferUtilities.hpp"
#include "math/Frustum.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>

namespace render::gpudriven
{
    GPUDrivenCameraBuffer::GPUDrivenCameraBuffer(core::Device& device, core::SwapChain& swapChain)
        : device(device)
        , swapChain(swapChain)
    {
    }

    GPUDrivenCameraBuffer::~GPUDrivenCameraBuffer()
    {
        cleanup();
    }

    void GPUDrivenCameraBuffer::init()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        core::BufferInfoRequest request(logicalDevice, physicalDevice);
        request.size = sizeof(GPUCameraData);
        request.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                            vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(request, buffer, memory);
        mapped = logicalDevice.mapMemory(memory, 0, sizeof(GPUCameraData), vk::MemoryMapFlags{});
    }

    void GPUDrivenCameraBuffer::cleanup()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (mapped)
        {
            logicalDevice.unmapMemory(memory);
            mapped = nullptr;
        }

        core::BufferUtilities::destroyBuffer(logicalDevice, buffer, memory);
    }

    void GPUDrivenCameraBuffer::update(const CameraUpdateParams& params)
    {
        glm::mat4 viewProjection = params.projection * params.view;

        data.view = params.view;
        data.projection = params.projection;
        data.viewProjection = viewProjection;
        data.invViewProjection = glm::inverse(viewProjection);

        data.cameraPosition = glm::vec4(params.cameraPosition, params.nearPlane);

        float screenW = (params.screenWidth > 0)
            ? static_cast<float>(params.screenWidth)
            : static_cast<float>(swapChain.getSwapchainExtent().width);
        float screenH = (params.screenHeight > 0)
            ? static_cast<float>(params.screenHeight)
            : static_cast<float>(swapChain.getSwapchainExtent().height);
        data.screenParams = glm::vec4(screenW, screenH, 1.0f / screenW, 1.0f / screenH);

        extractFrustumPlanes(viewProjection, data.frustumPlanes);

        data.farPlane = params.farPlane;
        data.objectCount = params.objectCount;
        data.hiZMipLevels = params.hiZMipLevels;
        data.frameIndex = frameIndex++;

        data.enableFrustumCulling = params.frustumCullingEnabled ? 1 : 0;
        data.enableOcclusionCulling = (params.occlusionCullingEnabled && params.hiZMipLevels > 0) ? 1 : 0;
        data.enableLODSelection = params.lodSelectionEnabled ? 1 : 0;
        data.batchCount = params.batchManager ? params.batchManager->getBatchCount() : 1;

        data.commandsPerBatch = params.batchManager ? params.batchManager->getCommandsPerBatch() : MAX_DRAW_COMMANDS;
        data.shaderGroupCount = params.batchManager ? params.batchManager->getShaderGroupCount() : MAX_SHADER_GROUPS;
        data.enableDistanceCulling = params.distanceCullingEnabled ? 1 : 0;
        data.globalLodBias = params.globalLodBias;

        const float* d = params.categoryDistances;
        data.categoryDistSq0 = glm::vec4(d[0] * d[0], d[1] * d[1], d[2] * d[2], d[3] * d[3]);
        data.categoryDistSq1 = glm::vec4(d[4] * d[4], d[5] * d[5], d[6] * d[6], params.shadowDistanceMultiplier);

        std::memcpy(mapped, &data, sizeof(GPUCameraData));
    }

    void GPUDrivenCameraBuffer::extractFrustumPlanes(const glm::mat4& viewProjection, glm::vec4 planes[6])
    {
        // Use the same extraction as CPU-side frustum culling (Frustum::extractFromMatrix)
        // The projection already has Y-flip applied, and CPU culling works correctly with it
        math::extractFrustumPlanes(viewProjection, planes);
    }
}
