#include "VFXMeshGPUPipeline.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Texture.hpp"
#include "../../core/DeferredDeletionQueue.hpp"
#include "print/Logger.hpp"
#include "GPUVFXTypes.hpp"


namespace render::vfx
{
    VFXMeshGPUPipeline::VFXMeshGPUPipeline(core::Device& device, core::SwapChain& swapChain,
                                             render::mesh::MeshGPUCache& meshCache)
        : device(device)
        , swapChain(swapChain)
        , meshCache(meshCache)
    {
    }

    VFXMeshGPUPipeline::~VFXMeshGPUPipeline()
    {
        cleanup();
    }

    void VFXMeshGPUPipeline::init(vk::RenderPass renderPass)
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
                loggerError("VFXMeshGPUPipeline: Failed to load shader");
                return;
            }

            createDescriptorSetLayout();
            if (!descriptorSetLayout)
            {
                loggerError("VFXMeshGPUPipeline: Failed to create descriptor set layout");
                return;
            }

            createDescriptorPool();
            if (!descriptorPool)
            {
                loggerError("VFXMeshGPUPipeline: Failed to create descriptor pool");
                cleanup();
                return;
            }

            createBuffers();
            if (!cameraUBO)
            {
                loggerError("VFXMeshGPUPipeline: Failed to create buffers");
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
                loggerError("VFXMeshGPUPipeline: Failed to create graphics pipeline");
                cleanup();
                return;
            }

            initialized = true;
            loggerInfo("VFXMeshGPUPipeline initialized");
        }
        catch (const vk::SystemError& e)
        {
            loggerError("VFXMeshGPUPipeline: Vulkan error during init - {}", e.what());
            cleanup();
        }
        catch (const std::exception& e)
        {
            loggerError("VFXMeshGPUPipeline: Exception during init - {}", e.what());
            cleanup();
        }
    }

    void VFXMeshGPUPipeline::recreate(vk::RenderPass renderPass)
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

    void VFXMeshGPUPipeline::cleanup()
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
        emitterMeshes.clear();

        if (gpuShader)
        {
            gpuShader->cleanUp();
            gpuShader.reset();
        }

        cachedParticleBuffer = nullptr;
        cachedParticleBufferSize = 0;
        cachedConfigBuffer = nullptr;
        cachedConfigBufferSize = 0;
        sceneDepthImageView = nullptr;
        descriptorsNeedUpdate = true;
        initialized = false;

        loggerInfo("VFXMeshGPUPipeline cleaned up");
    }

    void VFXMeshGPUPipeline::loadShader()
    {
        gpuShader = std::make_shared<core::Shader>(device);
        gpuShader->readShader("../../resources/shaders/vfx/vfx_mesh_particle.glsl");

        if (gpuShader->getShaderStages().empty())
        {
            loggerError("VFXMeshGPUPipeline: Failed to load shader: {}",
                        gpuShader->getLastCompilationError());
        }
    }

    void VFXMeshGPUPipeline::createDescriptorSetLayout()
    {
        std::array<vk::DescriptorSetLayoutBinding, 5> bindings{};

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

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void VFXMeshGPUPipeline::createDescriptorPool()
    {
        uint32_t totalSets = MAX_TEXTURE_SLOTS + 1;

        std::array<vk::DescriptorPoolSize, 3> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = totalSets;
        poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = totalSets * 2;
        poolSizes[2].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[2].descriptorCount = totalSets * 2;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = totalSets;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void VFXMeshGPUPipeline::allocateDescriptorSet()
    {
        defaultDescriptorSet = allocateDescriptorSetFromPool();
    }

    vk::DescriptorSet VFXMeshGPUPipeline::allocateDescriptorSetFromPool()
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

    void VFXMeshGPUPipeline::createPipeline()
    {
        auto vertexBinding = render::mesh::MeshVertexInput::getBindingDescription();
        auto vertexAttribs = render::mesh::MeshVertexInput::getAttributeDescriptions();

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
            .cullMode = vk::CullModeFlagBits::eBack,
            .depthTestEnable = true,
            .depthWriteEnable = false,
            .blendEnable = true
        };

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
    }

    void VFXMeshGPUPipeline::createBuffers()
    {
        auto vkDevice = device.getLogicalDevice();

        core::BufferInfoRequest uboRequest(vkDevice, device.getPhysicalDevice());
        uboRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        uboRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
        uboRequest.size = sizeof(GPUVFXCameraUBO);
        core::BufferUtilities::createBuffer(uboRequest, cameraUBO, cameraUBOMemory);
        cameraUBOMapped = vkDevice.mapMemory(cameraUBOMemory, 0, sizeof(GPUVFXCameraUBO));
    }

    void VFXMeshGPUPipeline::createDefaultTexture()
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

    void VFXMeshGPUPipeline::createSampler()
    {
        textureSampler = core::ImageUtilities::createVFXSampler(device.getLogicalDevice());
    }

    void VFXMeshGPUPipeline::createDepthSampler()
    {
        depthSampler = core::ImageUtilities::createVFXDepthSampler(device.getLogicalDevice());
    }
}
