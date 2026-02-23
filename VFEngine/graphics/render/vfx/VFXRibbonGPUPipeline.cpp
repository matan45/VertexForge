#include "VFXRibbonGPUPipeline.hpp"
#include "VFXQuadData.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/DeferredDeletionQueue.hpp"
#include "print/Logger.hpp"
#include "GPUVFXTypes.hpp"



namespace render::vfx
{
    VFXRibbonGPUPipeline::VFXRibbonGPUPipeline(core::Device& device, core::SwapChain& swapChain)
        : device(device)
        , swapChain(swapChain)
    {
    }

    VFXRibbonGPUPipeline::~VFXRibbonGPUPipeline()
    {
        cleanup();
    }

    void VFXRibbonGPUPipeline::init(vk::RenderPass renderPass)
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
                loggerError("VFXRibbonGPUPipeline: Failed to load shader");
                return;
            }

            createDescriptorSetLayout();
            if (!descriptorSetLayout)
            {
                loggerError("VFXRibbonGPUPipeline: Failed to create descriptor set layout");
                return;
            }

            createDescriptorPool();
            if (!descriptorPool)
            {
                loggerError("VFXRibbonGPUPipeline: Failed to create descriptor pool");
                cleanup();
                return;
            }

            createBuffers();
            if (!cameraUBO)
            {
                loggerError("VFXRibbonGPUPipeline: Failed to create buffers");
                cleanup();
                return;
            }

            createDefaultTexture();
            createSampler();
            createDepthSampler();
            allocateDescriptorSet();

            createPipeline();
            if (!graphicsPipeline || !pipelineLayout)
            {
                loggerError("VFXRibbonGPUPipeline: Failed to create graphics pipeline");
                cleanup();
                return;
            }

            initialized = true;
            loggerInfo("VFXRibbonGPUPipeline initialized");
        }
        catch (const vk::SystemError& e)
        {
            loggerError("VFXRibbonGPUPipeline: Vulkan error during init - {}", e.what());
            cleanup();
        }
        catch (const std::exception& e)
        {
            loggerError("VFXRibbonGPUPipeline: Exception during init - {}", e.what());
            cleanup();
        }
    }

    void VFXRibbonGPUPipeline::recreate(vk::RenderPass renderPass)
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

    void VFXRibbonGPUPipeline::cleanup()
    {
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

        if (cameraUBOMapped && cameraUBOMemory)
        {
            vkDevice.unmapMemory(cameraUBOMemory);
            cameraUBOMapped = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice, cameraUBO, cameraUBOMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, quadVertexBuffer, quadVertexBufferMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, quadIndexBuffer, quadIndexBufferMemory);

        if (depthSampler)
        {
            vkDevice.destroySampler(depthSampler);
            depthSampler = nullptr;
        }

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
        cachedRibbonRingBuffer = nullptr;
        cachedRibbonRingBufferSize = 0;
        cachedRibbonHeadBuffer = nullptr;
        cachedRibbonHeadBufferSize = 0;
        sceneDepthImageView = nullptr;
        descriptorsNeedUpdate = true;
        initialized = false;

        loggerInfo("VFXRibbonGPUPipeline cleaned up");
    }

    void VFXRibbonGPUPipeline::loadShader()
    {
        gpuShader = std::make_shared<core::Shader>(device);
        gpuShader->readShader("../../resources/shaders/vfx/vfx_ribbon_gpu.glsl");

        if (gpuShader->getShaderStages().empty())
        {
            loggerError("VFXRibbonGPUPipeline: Failed to load shader: {}",
                        gpuShader->getLastCompilationError());
        }
    }

    void VFXRibbonGPUPipeline::createDescriptorSetLayout()
    {
        std::array<vk::DescriptorSetLayoutBinding, 7> bindings{};

        // Binding 0: Camera UBO
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

        // Binding 1: Particle texture
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

        // Binding 2: Particle SSBO
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eVertex;

        // Binding 3: Emitter config SSBO
        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

        // Binding 4: Scene depth texture (soft particles)
        bindings[4].binding = 4;
        bindings[4].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = vk::ShaderStageFlagBits::eFragment;

        bindings[5].binding = 5;
        bindings[5].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags = vk::ShaderStageFlagBits::eVertex;

        bindings[6].binding = 6;
        bindings[6].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[6].descriptorCount = 1;
        bindings[6].stageFlags = vk::ShaderStageFlagBits::eVertex;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void VFXRibbonGPUPipeline::createDescriptorPool()
    {
        uint32_t totalSets = MAX_TEXTURE_SLOTS + 1;

        std::array<vk::DescriptorPoolSize, 3> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = totalSets;
        poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = totalSets * 2; // particle texture + depth texture
        poolSizes[2].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[2].descriptorCount = totalSets * 4; // particle + config + ring + head

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = totalSets;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void VFXRibbonGPUPipeline::allocateDescriptorSet()
    {
        defaultDescriptorSet = allocateDescriptorSetFromPool();
    }

    vk::DescriptorSet VFXRibbonGPUPipeline::allocateDescriptorSetFromPool()
    {
        // Recycle descriptor sets that have aged past the deferred deletion window
        for (auto it = pendingDescriptorSets.begin(); it != pendingDescriptorSets.end(); ++it)
        {
            if (frameCounter - it->frameRetired >= core::DeferredDeletionQueue::FRAMES_BEFORE_DELETE)
            {
                auto recycled = it->set;
                pendingDescriptorSets.erase(it);
                return recycled;
            }
        }

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        return device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
    }

    void VFXRibbonGPUPipeline::createPipeline()
    {
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
            .depthWriteEnable = false,
            .blendEnable = true
        };

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
    }

    void VFXRibbonGPUPipeline::createBuffers()
    {
        auto vkDevice = device.getLogicalDevice();

        core::BufferInfoRequest uboRequest(vkDevice, device.getPhysicalDevice());
        uboRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        uboRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
        uboRequest.size = sizeof(GPUVFXCameraUBO);
        core::BufferUtilities::createBuffer(uboRequest, cameraUBO, cameraUBOMemory);
        cameraUBOMapped = vkDevice.mapMemory(cameraUBOMemory, 0, sizeof(GPUVFXCameraUBO));

        constexpr vk::DeviceSize vertexBufferSize = sizeof(VFXQuadVertex) * QUAD_VERTICES.size();
        core::BufferInfoRequest vertexRequest(vkDevice, device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer |
                              vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, quadVertexBuffer, quadVertexBufferMemory);

        constexpr vk::DeviceSize indexBufferSize = sizeof(uint16_t) * QUAD_INDICES.size();
        core::BufferInfoRequest indexRequest(vkDevice, device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer |
                             vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, quadIndexBuffer, quadIndexBufferMemory);

        core::BufferUtilities::copyToBuffer(
            vkDevice,
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            quadVertexBuffer,
            QUAD_VERTICES.data(),
            vertexBufferSize
        );

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

    void VFXRibbonGPUPipeline::createDefaultTexture()
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
        core::ImageUtilities::uploadStagedPixelData(device, defaultTextureImage, whitePixel.data(), whitePixel.size(), texSize, texSize);
    }

    void VFXRibbonGPUPipeline::createSampler()
    {
        textureSampler = core::ImageUtilities::createVFXSampler(device.getLogicalDevice());
    }

    void VFXRibbonGPUPipeline::createDepthSampler()
    {
        depthSampler = core::ImageUtilities::createVFXDepthSampler(device.getLogicalDevice());
    }
}
