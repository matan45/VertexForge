#include "GPUDrivenRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/ImageUtilities.hpp"
#include "print/Log.hpp"
#include <fstream>
#include <array>
#include <cmath>
#include <cstring>

namespace render::gpudriven
{
    void GPUDrivenRenderer::initBillboardSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout,
                                                      vk::RenderPass renderPass)
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
            renderPass
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

    uint32_t GPUDrivenRenderer::loadImposterAtlas(const std::string& imposterPath)
    {
        // Already loaded?
        auto it = billboard.loadedImposters.find(imposterPath);
        if (it != billboard.loadedImposters.end())
        {
            return it->second.bindlessIndex;
        }

        // Read .vfImposter binary
        static constexpr std::array<char, 4> IMPOSTER_MAGIC = { 'V', 'F', 'I', 'M' };

        std::ifstream file(imposterPath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("GPUDrivenRenderer: Failed to open imposter file: {}", imposterPath);
            return 0;
        }

        // Validate magic
        std::array<char, 4> magic{};
        file.read(magic.data(), 4);
        if (magic != IMPOSTER_MAGIC)
        {
            vfLogError("GPUDrivenRenderer: Invalid imposter file magic: {}", imposterPath);
            return 0;
        }

        // Read header
        uint32_t version = 0, atlasWidth = 0, atlasHeight = 0, viewCount = 0;
        uint8_t hasNormalMap = 0;
        uint32_t hAngles = 0, vAngles = 0, viewRes = 0;

        file.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&atlasWidth), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&atlasHeight), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&viewCount), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&hasNormalMap), sizeof(uint8_t));
        file.read(reinterpret_cast<char*>(&hAngles), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&vAngles), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&viewRes), sizeof(uint32_t));

        if (atlasWidth == 0 || atlasHeight == 0 || atlasWidth > 16384 || atlasHeight > 16384)
        {
            vfLogError("GPUDrivenRenderer: Invalid atlas dimensions {}x{} in: {}", atlasWidth, atlasHeight, imposterPath);
            return 0;
        }

        // Read view infos (2 floats angles + 4 floats uvRect per view)
        std::vector<ImposterViewInfo> views(viewCount);
        for (uint32_t i = 0; i < viewCount; ++i)
        {
            file.read(reinterpret_cast<char*>(&views[i].horizontalAngle), sizeof(float));
            file.read(reinterpret_cast<char*>(&views[i].verticalAngle), sizeof(float));
            file.read(reinterpret_cast<char*>(&views[i].uvRect), sizeof(float) * 4);
        }

        // Read color data
        size_t pixelDataSize = static_cast<size_t>(atlasWidth) * atlasHeight * 4;
        std::vector<uint8_t> colorData(pixelDataSize);
        file.read(reinterpret_cast<char*>(colorData.data()), static_cast<std::streamsize>(pixelDataSize));

        if (!file.good())
        {
            vfLogError("GPUDrivenRenderer: Failed to read imposter color data: {}", imposterPath);
            return 0;
        }

        // Create GPU image
        vk::Device vkDevice = device.getLogicalDevice();
        vk::PhysicalDevice physDevice = device.getPhysicalDevice();

        ImposterTexture tex;
        tex.width = atlasWidth;
        tex.height = atlasHeight;
        tex.hAngles = hAngles;
        tex.vAngles = vAngles;
        tex.atlasCols = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(viewCount))));
        tex.views = std::move(views);

        core::ImageInfoRequest imageReq(vkDevice, physDevice, atlasWidth, atlasHeight);
        imageReq.format = vk::Format::eR8G8B8A8Unorm;
        imageReq.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;

        core::ImageUtilities::createImage(imageReq, tex.image, tex.memory);

        // Upload pixel data
        core::ImageUtilities::uploadStagedPixelData(device, tex.image,
            colorData.data(), static_cast<vk::DeviceSize>(pixelDataSize),
            atlasWidth, atlasHeight);

        // Create image view
        core::ImageViewInfoRequest viewReq(vkDevice, tex.image, vk::Format::eR8G8B8A8Unorm);
        core::ImageUtilities::createImageView(viewReq, tex.imageView);

        // Create sampler (linear filtering, clamp to edge)
        tex.sampler = core::ImageUtilities::createVFXSampler(vkDevice);

        // Register with bindless texture manager
        tex.bindlessIndex = bindlessTextures->registerTexture(imposterPath, tex.imageView, tex.sampler);

        if (tex.bindlessIndex == 0xFFFFFFFF)
        {
            vfLogError("GPUDrivenRenderer: Failed to register imposter texture: {}", imposterPath);
            vkDevice.destroySampler(tex.sampler);
            vkDevice.destroyImageView(tex.imageView);
            vkDevice.destroyImage(tex.image);
            vkDevice.freeMemory(tex.memory);
            return 0;
        }

        vfLogInfo("GPUDrivenRenderer: Loaded imposter atlas {} ({}x{}, bindless={})",
                  imposterPath, atlasWidth, atlasHeight, tex.bindlessIndex);

        billboard.loadedImposters[imposterPath] = tex;
        return tex.bindlessIndex;
    }

    void GPUDrivenRenderer::unloadImposterAtlas(const std::string& imposterPath)
    {
        auto it = billboard.loadedImposters.find(imposterPath);
        if (it == billboard.loadedImposters.end()) return;

        vk::Device vkDevice = device.getLogicalDevice();
        auto& tex = it->second;

        bindlessTextures->unregisterTexture(imposterPath);

        vkDevice.destroySampler(tex.sampler);
        vkDevice.destroyImageView(tex.imageView);
        vkDevice.destroyImage(tex.image);
        vkDevice.freeMemory(tex.memory);

        billboard.loadedImposters.erase(it);
    }

    bool GPUDrivenRenderer::hasImposterAtlas(const std::string& imposterPath) const
    {
        return billboard.loadedImposters.contains(imposterPath);
    }
}
