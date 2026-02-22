#include "VFXSceneGPUPipeline.hpp"
#include "VFXQuadData.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Texture.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "print/Logger.hpp"
#include "GPUVFXTypes.hpp"
#include <filesystem>

namespace
{
    void uploadStagedPixelData(core::Device& device, vk::Image image,
                               const void* pixelData, vk::DeviceSize imageSize,
                               uint32_t width, uint32_t height)
    {
        auto vkDevice = device.getLogicalDevice();

        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;
        core::BufferInfoRequest stagingRequest(vkDevice, device.getPhysicalDevice());
        stagingRequest.size = imageSize;
        stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(stagingRequest, stagingBuffer, stagingMemory);

        void* data = vkDevice.mapMemory(stagingMemory, 0, imageSize);
        std::memcpy(data, pixelData, imageSize);
        vkDevice.unmapMemory(stagingMemory);

        auto cmd = core::Utilities::beginSingleTimeCommands(vkDevice, device.getStagingCommandPool());

        core::ImageUtilities::transitionImageLayout(
            cmd.get(), image,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageAspectFlagBits::eColor);

        vk::BufferImageCopy region{};
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = vk::Extent3D{width, height, 1};

        cmd->copyBufferToImage(stagingBuffer, image,
                               vk::ImageLayout::eTransferDstOptimal, region);

        core::ImageUtilities::transitionImageLayout(
            cmd.get(), image,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmd);

        vkDevice.destroyBuffer(stagingBuffer);
        vkDevice.freeMemory(stagingMemory);
    }
}

namespace render::vfx
{
    VFXSceneGPUPipeline::VFXSceneGPUPipeline(core::Device& device, core::SwapChain& swapChain)
        : device(device)
        , swapChain(swapChain)
    {
    }

    VFXSceneGPUPipeline::~VFXSceneGPUPipeline()
    {
        cleanup();
    }

    void VFXSceneGPUPipeline::init(vk::RenderPass renderPass)
    {
        if (initialized)
        {
            return;
        }

        externalRenderPass = renderPass;

        try
        {
            loadShader();
            if (!gpuShader)
            {
                loggerError("VFXSceneGPUPipeline: Failed to load shader");
                return;
            }

            createDescriptorSetLayout();
            if (!descriptorSetLayout)
            {
                loggerError("VFXSceneGPUPipeline: Failed to create descriptor set layout");
                return;
            }

            createDescriptorPool();
            if (!descriptorPool)
            {
                loggerError("VFXSceneGPUPipeline: Failed to create descriptor pool");
                cleanup();
                return;
            }

            createBuffers();
            if (!cameraUBO || !quadVertexBuffer || !quadIndexBuffer)
            {
                loggerError("VFXSceneGPUPipeline: Failed to create buffers");
                cleanup();
                return;
            }

            createDefaultTexture();
            createSampler();
            allocateDescriptorSet();

            createPipeline();
            if (!graphicsPipeline || !pipelineLayout)
            {
                loggerError("VFXSceneGPUPipeline: Failed to create graphics pipeline");
                cleanup();
                return;
            }

            initialized = true;
            loggerInfo("VFXSceneGPUPipeline initialized");
        }
        catch (const vk::SystemError& e)
        {
            loggerError("VFXSceneGPUPipeline: Vulkan error during init - {}", e.what());
            cleanup();
        }
        catch (const std::exception& e)
        {
            loggerError("VFXSceneGPUPipeline: Exception during init - {}", e.what());
            cleanup();
        }
    }

    void VFXSceneGPUPipeline::recreate(vk::RenderPass renderPass)
    {
        if (!initialized)
        {
            return;
        }

        externalRenderPass = renderPass;

        auto vkDevice = device.getLogicalDevice();
        vkDevice.destroyPipeline(graphicsPipeline);
        vkDevice.destroyPipelineLayout(pipelineLayout);

        createPipeline();
    }

    void VFXSceneGPUPipeline::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        auto vkDevice = device.getLogicalDevice();

        if (graphicsPipeline)
        {
            vkDevice.destroyPipeline(graphicsPipeline);
            graphicsPipeline = nullptr;
        }

        if (pipelineLayout)
        {
            vkDevice.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        if (descriptorPool)
        {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (descriptorSetLayout)
        {
            vkDevice.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        // Unmap and clean up buffers
        if (cameraUBOMapped && cameraUBOMemory)
        {
            vkDevice.unmapMemory(cameraUBOMemory);
            cameraUBOMapped = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice, cameraUBO, cameraUBOMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, quadVertexBuffer, quadVertexBufferMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, quadIndexBuffer, quadIndexBufferMemory);

        // Clean up texture
        if (textureSampler)
        {
            vkDevice.destroySampler(textureSampler);
            textureSampler = nullptr;
        }

        if (defaultTextureImageView)
        {
            vkDevice.destroyImageView(defaultTextureImageView);
            defaultTextureImageView = nullptr;
        }

        if (defaultTextureImage)
        {
            vkDevice.destroyImage(defaultTextureImage);
            vkDevice.freeMemory(defaultTextureMemory);
            defaultTextureImage = nullptr;
        }

        textureEntries.clear();
        emitterConfigs.clear();

        if (gpuShader)
        {
            gpuShader->cleanUp();
            gpuShader.reset();
        }

        cachedParticleBuffer = nullptr;
        cachedParticleBufferSize = 0;
        cachedConfigBuffer = nullptr;
        cachedConfigBufferSize = 0;
        descriptorsNeedUpdate = true;
        initialized = false;

        loggerInfo("VFXSceneGPUPipeline cleaned up");
    }

    void VFXSceneGPUPipeline::loadShader()
    {
        gpuShader = std::make_shared<core::Shader>(device);
        gpuShader->readShader("../../resources/shaders/vfx/vfx_billboard_gpu.glsl");

        if (gpuShader->getShaderStages().empty())
        {
            loggerError("VFXSceneGPUPipeline: Failed to load shader: {}",
                        gpuShader->getLastCompilationError());
        }
    }

    void VFXSceneGPUPipeline::createDescriptorSetLayout()
    {
        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};

        // Binding 0: Camera UBO
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex;

        // Binding 1: Particle texture
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

        // Binding 2: Particle SSBO (read by vertex shader)
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eVertex;

        // Binding 3: Emitter config SSBO (VK-493: flipbook config access in vertex shader)
        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eVertex;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void VFXSceneGPUPipeline::createDescriptorPool()
    {
        uint32_t totalSets = MAX_TEXTURE_SLOTS + 1;  // +1 for default descriptor set

        std::array<vk::DescriptorPoolSize, 3> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = totalSets;
        poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = totalSets;
        poolSizes[2].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[2].descriptorCount = totalSets * 2;  // particle SSBO + config SSBO per set

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = totalSets;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void VFXSceneGPUPipeline::allocateDescriptorSet()
    {
        defaultDescriptorSet = allocateDescriptorSetFromPool();
    }

    vk::DescriptorSet VFXSceneGPUPipeline::allocateDescriptorSetFromPool()
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        return device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
    }

    void VFXSceneGPUPipeline::updateParticleBuffer(vk::Buffer particleBuffer, vk::DeviceSize particleBufferSize)
    {
        if (particleBuffer != cachedParticleBuffer || particleBufferSize != cachedParticleBufferSize)
        {
            cachedParticleBuffer = particleBuffer;
            cachedParticleBufferSize = particleBufferSize;
            descriptorsNeedUpdate = true;
        }
    }

    void VFXSceneGPUPipeline::updateConfigBuffer(vk::Buffer configBuffer, vk::DeviceSize configBufferSize)
    {
        if (configBuffer != cachedConfigBuffer || configBufferSize != cachedConfigBufferSize)
        {
            cachedConfigBuffer = configBuffer;
            cachedConfigBufferSize = configBufferSize;
            descriptorsNeedUpdate = true;
        }
    }

    void VFXSceneGPUPipeline::writeDescriptors() const
    {
        if (!descriptorsNeedUpdate || !cachedParticleBuffer || !cachedConfigBuffer)
        {
            return;
        }

        // Update default descriptor set
        writeDescriptorSet(defaultDescriptorSet, nullptr);

        // Update all texture descriptor sets
        for (const auto& [path, entry] : textureEntries)
        {
            writeDescriptorSet(entry.descriptorSet, entry.texture.get());
        }

        descriptorsNeedUpdate = false;
    }

    void VFXSceneGPUPipeline::writeDescriptorSet(vk::DescriptorSet dstSet, core::Texture* texture) const
    {
        auto vkDevice = device.getLogicalDevice();

        // Camera UBO
        vk::DescriptorBufferInfo cameraInfo{};
        cameraInfo.buffer = cameraUBO;
        cameraInfo.offset = 0;
        cameraInfo.range = sizeof(GPUVFXCameraUBO);

        // Texture
        vk::DescriptorImageInfo textureInfo{};
        textureInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        if (texture)
        {
            textureInfo.imageView = texture->getImageView();
            textureInfo.sampler = texture->getSampler();
        }
        else
        {
            textureInfo.imageView = defaultTextureImageView;
            textureInfo.sampler = textureSampler;
        }

        // Particle SSBO
        vk::DescriptorBufferInfo particleInfo{};
        particleInfo.buffer = cachedParticleBuffer;
        particleInfo.offset = 0;
        particleInfo.range = cachedParticleBufferSize;

        // Emitter config SSBO
        vk::DescriptorBufferInfo configInfo{};
        configInfo.buffer = cachedConfigBuffer;
        configInfo.offset = 0;
        configInfo.range = cachedConfigBufferSize;

        std::array<vk::WriteDescriptorSet, 4> writes{};

        writes[0].dstSet = dstSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[0].pBufferInfo = &cameraInfo;

        writes[1].dstSet = dstSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[1].pImageInfo = &textureInfo;

        writes[2].dstSet = dstSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[2].pBufferInfo = &particleInfo;

        writes[3].dstSet = dstSet;
        writes[3].dstBinding = 3;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[3].pBufferInfo = &configInfo;

        vkDevice.updateDescriptorSets(writes, {});
    }

    void VFXSceneGPUPipeline::createPipeline()
    {
        // Only need quad vertex binding - particle data comes from SSBO
        auto vertexBinding = VFXQuadVertex::getBindingDescription();
        auto vertexAttribs = VFXQuadVertex::getAttributeDescriptions();

        core::GraphicsPipelineConfig config{
            .device = device.getLogicalDevice(),
            .renderPass = externalRenderPass,
            .extent = swapChain.getSwapchainExtent(),
            .shaderStages = gpuShader->getShaderStages(),
            .vertexBindings = {vertexBinding},
            .vertexAttributes = {vertexAttribs.begin(), vertexAttribs.end()},
            .topology = vk::PrimitiveTopology::eTriangleList,
            .descriptorSetLayouts = {descriptorSetLayout},
            .pushConstantSize = sizeof(GPUVFXBillboardPushConstants),
            .pushConstantStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .cullMode = vk::CullModeFlagBits::eNone,
            .depthTestEnable = true,
            .depthWriteEnable = false,  // Particles don't write depth
            .blendEnable = true         // Alpha blending for particles
        };

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
    }

    void VFXSceneGPUPipeline::createBuffers()
    {
        auto vkDevice = device.getLogicalDevice();

        // Camera UBO (persistently mapped for efficient per-frame updates)
        core::BufferInfoRequest uboRequest(vkDevice, device.getPhysicalDevice());
        uboRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        uboRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
        uboRequest.size = sizeof(GPUVFXCameraUBO);
        core::BufferUtilities::createBuffer(uboRequest, cameraUBO, cameraUBOMemory);
        cameraUBOMapped = vkDevice.mapMemory(cameraUBOMemory, 0, sizeof(GPUVFXCameraUBO));

        // Quad vertex buffer
        constexpr vk::DeviceSize vertexBufferSize = sizeof(VFXQuadVertex) * QUAD_VERTICES.size();
        core::BufferInfoRequest vertexRequest(vkDevice, device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer |
                              vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, quadVertexBuffer, quadVertexBufferMemory);

        // Quad index buffer
        constexpr vk::DeviceSize indexBufferSize = sizeof(uint16_t) * QUAD_INDICES.size();
        core::BufferInfoRequest indexRequest(vkDevice, device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer |
                             vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, quadIndexBuffer, quadIndexBufferMemory);

        // Upload quad vertices
        core::BufferUtilities::copyToBuffer(
            vkDevice,
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            quadVertexBuffer,
            QUAD_VERTICES.data(),
            vertexBufferSize
        );

        // Upload quad indices
        core::BufferUtilities::copyToBuffer(
            vkDevice,
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            quadIndexBuffer,
            QUAD_INDICES.data(),
            indexBufferSize
        );
    }

    void VFXSceneGPUPipeline::createDefaultTexture()
    {
        auto vkDevice = device.getLogicalDevice();
        constexpr uint32_t texSize = 1;

        core::ImageInfoRequest imageInfo(
            vkDevice, device.getPhysicalDevice(),
            texSize, texSize, 1, 1,
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(imageInfo, defaultTextureImage, defaultTextureMemory);

        core::ImageViewInfoRequest viewInfo(
            vkDevice, defaultTextureImage,
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D
        );
        core::ImageUtilities::createImageView(viewInfo, defaultTextureImageView);

        const std::array<uint8_t, 4> whitePixel = {255, 255, 255, 255};
        uploadStagedPixelData(device, defaultTextureImage, whitePixel.data(), whitePixel.size(), texSize, texSize);
    }

    void VFXSceneGPUPipeline::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eAlways;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        textureSampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void VFXSceneGPUPipeline::updateCameraUBO(
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPos,
        float time) const
    {
        if (!cameraUBOMapped)
        {
            return;
        }

        GPUVFXCameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;
        ubo.time = time;

        // Use persistent mapping - no map/unmap overhead
        std::memcpy(cameraUBOMapped, &ubo, sizeof(ubo));
    }

    void VFXSceneGPUPipeline::setEmitterTexture(uint32_t emitterIndex, const std::string& texturePath)
    {
        emitterConfigs[emitterIndex].texturePath = texturePath;

        if (texturePath.empty())
        {
            return;  // Will use default descriptor set
        }

        // Check if texture already loaded
        if (textureEntries.count(texturePath))
        {
            return;
        }

        if (!std::filesystem::exists(texturePath))
        {
            loggerWarning("VFX GPU texture not found: {}", texturePath);
            emitterConfigs[emitterIndex].texturePath.clear();
            return;
        }

        if (textureEntries.size() >= MAX_TEXTURE_SLOTS)
        {
            loggerWarning("Max VFX texture slots ({}) reached, emitter {} will use default texture",
                          MAX_TEXTURE_SLOTS, emitterIndex);
            emitterConfigs[emitterIndex].texturePath.clear();
            return;
        }

        device.getLogicalDevice().waitIdle();

        try
        {
            auto& entry = textureEntries[texturePath];
            entry.texture = std::make_unique<core::Texture>(device);
            entry.texture->loadTextureFromFile(texturePath, vk::Format::eR8G8B8A8Srgb, false);
            entry.descriptorSet = allocateDescriptorSetFromPool();

            if (cachedParticleBuffer && cachedConfigBuffer)
            {
                writeDescriptorSet(entry.descriptorSet, entry.texture.get());
            }
            else
            {
                descriptorsNeedUpdate = true;
            }

            loggerInfo("VFX GPU texture loaded: {}", texturePath);
        }
        catch (const std::exception& e)
        {
            loggerError("Failed to load VFX GPU texture '{}': {}", texturePath, e.what());
            textureEntries.erase(texturePath);
            emitterConfigs[emitterIndex].texturePath.clear();
        }
    }

    void VFXSceneGPUPipeline::setEmitterRenderingConfig(uint32_t emitterIndex,
                                                         float alphaClipThreshold, bool additiveBlend)
    {
        emitterConfigs[emitterIndex].alphaClipThreshold = alphaClipThreshold;
        emitterConfigs[emitterIndex].blendMode = additiveBlend ? 1u : 0u;
    }

    void VFXSceneGPUPipeline::removeEmitter(uint32_t emitterIndex)
    {
        emitterConfigs.erase(emitterIndex);
    }

    void VFXSceneGPUPipeline::recordCommandsInline(
        vk::CommandBuffer cmd,
        vk::Buffer drawCommandBuffer,
        uint32_t emitterCount) const
    {
        if (!initialized || emitterCount == 0 || !cachedParticleBuffer)
        {
            return;
        }

        // Write descriptors if needed (uses mutable flag for lazy updates)
        writeDescriptors();

        // Bind pipeline
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        // Bind only quad vertex buffer (no instance buffer - data comes from SSBO)
        vk::Buffer vertexBuffers[] = {quadVertexBuffer};
        vk::DeviceSize offsets[] = {0};
        cmd.bindVertexBuffers(0, 1, vertexBuffers, offsets);

        // Bind index buffer
        cmd.bindIndexBuffer(quadIndexBuffer, 0, vk::IndexType::eUint16);

        // Issue indirect draws per emitter with per-emitter descriptor set and push constants
        vk::DescriptorSet lastBoundSet = nullptr;

        for (uint32_t i = 0; i < emitterCount; ++i)
        {
            // Look up per-emitter config
            vk::DescriptorSet setToBind = defaultDescriptorSet;
            float alphaClip = 0.1f;
            uint32_t blendMode = 0;

            auto configIt = emitterConfigs.find(i);
            if (configIt != emitterConfigs.end())
            {
                alphaClip = configIt->second.alphaClipThreshold;
                blendMode = configIt->second.blendMode;

                if (!configIt->second.texturePath.empty())
                {
                    auto texIt = textureEntries.find(configIt->second.texturePath);
                    if (texIt != textureEntries.end())
                    {
                        setToBind = texIt->second.descriptorSet;
                    }
                }
            }

            // Only rebind descriptor set if changed
            if (setToBind != lastBoundSet)
            {
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                       0, setToBind, {});
                lastBoundSet = setToBind;
            }

            // Per-emitter push constants
            GPUVFXBillboardPushConstants pushConstants{};
            pushConstants.emitterIndex = i;
            pushConstants.alphaClipThreshold = alphaClip;
            pushConstants.blendMode = blendMode;
            cmd.pushConstants(pipelineLayout,
                              vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                              0, sizeof(GPUVFXBillboardPushConstants), &pushConstants);

            vk::DeviceSize offset = i * sizeof(VFXDrawIndirectCommand);
            cmd.drawIndexedIndirect(drawCommandBuffer, offset, 1, sizeof(VFXDrawIndirectCommand));
        }
    }
}
