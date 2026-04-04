#include "GPUDrivenRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "print/Log.hpp"

namespace render::gpudriven
{
    void GPUDrivenRenderer::initBillboardSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout,
                                                      const std::vector<vk::Format>& colorFormats, vk::Format depthFormat)
    {
        billboard.bufferManager = std::make_unique<BillboardBufferManager>();
        billboard.bufferManager->init(device);

        billboard.streamManager = std::make_unique<BillboardStreamManager>();
        billboard.streamManager->init(device);

        billboard.meshShaderPipeline = std::make_unique<BillboardMeshShaderPipeline>();
        billboard.meshShaderPipeline->init(
            device,
            vk::DescriptorSetLayout{},  // Camera layout created internally
            bindlessTextures->getDescriptorSetLayout(),
            colorFormats, depthFormat
        );

        if (billboard.meshShaderPipeline->isInitialized())
        {
            // Wire up instance buffer descriptors
            billboard.meshShaderPipeline->updateInstanceDescriptors(
                billboard.bufferManager->getInstanceBuffer(),
                billboard.bufferManager->getCountBuffer()
            );

            // Wire up camera UBO
            billboard.meshShaderPipeline->updateCameraDescriptor(cameraBuffer->getBuffer());

            // Wire up bindless texture descriptor
            billboard.meshShaderPipeline->updateSharedDescriptors(
                bindlessTextures->getDescriptorSet()
            );

            billboard.initialized = true;
            vfLogInfo("GPUDrivenRenderer: Billboard subsystems initialized");
        }
        else
        {
            vfLogError("GPUDrivenRenderer: Failed to initialize billboard subsystems");
        }
    }

    void GPUDrivenRenderer::updateBillboards(const std::vector<BillboardInstanceGPU>& instances)
    {
        if (!initialized || !billboard.initialized || !billboard.renderingEnabled) return;

        billboard.instanceList = instances;
        billboard.stats.totalInstances = static_cast<uint32_t>(instances.size());

        if (instances.empty())
        {
            billboard.bufferManager->clear();
            return;
        }

        billboard.bufferManager->uploadInstances(instances);

        // Re-update descriptors in case buffer was recreated
        billboard.meshShaderPipeline->updateInstanceDescriptors(
            billboard.bufferManager->getInstanceBuffer(),
            billboard.bufferManager->getCountBuffer()
        );
    }

    void GPUDrivenRenderer::renderBillboardDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                                                  uint32_t screenWidth, uint32_t screenHeight)
    {
        if (!initialized || !billboard.initialized || !billboard.renderingEnabled) return;

        // Reset GPU count buffer if a clear was requested
        if (billboard.bufferManager->needsCountReset())
        {
            billboard.bufferManager->resetCountBuffer(cmd);
        }

        uint32_t count = billboard.bufferManager->getInstanceCount();
        if (count == 0) return;

        // Update shared descriptor sets
        billboard.meshShaderPipeline->updateSharedDescriptors(
            bindlessTextures->getDescriptorSet()
        );

        // Set viewport and scissor
        float dispatchWidth, dispatchHeight;
        if (screenWidth > 0 && screenHeight > 0)
        {
            dispatchWidth = static_cast<float>(screenWidth);
            dispatchHeight = static_cast<float>(screenHeight);
        }
        else
        {
            auto extent = swapChain.getSwapchainExtent();
            dispatchWidth = static_cast<float>(extent.width);
            dispatchHeight = static_cast<float>(extent.height);
        }

        vk::Viewport viewport{0.0f, 0.0f, dispatchWidth, dispatchHeight, 0.0f, 1.0f};
        vk::Rect2D scissor{{0, 0}, {static_cast<uint32_t>(dispatchWidth), static_cast<uint32_t>(dispatchHeight)}};

        cmd.setViewport(0, 1, &viewport);
        cmd.setScissor(0, 1, &scissor);

        billboard.meshShaderPipeline->dispatch(cmd, count);

        billboard.stats.visibleInstances = count;
    }

    void GPUDrivenRenderer::clearBillboardData()
    {
        if (!billboard.initialized) return;

        billboard.instanceList.clear();
        billboard.bufferManager->clear();
        billboard.stats = {};
    }

}
