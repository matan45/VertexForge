#include "VFXSceneGPUPipeline.hpp"
#include "../quad/VFXQuadData.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/ImageUtilities.hpp"
#include "../../../core/DeferredDeletionQueue.hpp"
#include "print/Log.hpp"
#include "../compute/GPUVFXTypes.hpp"


namespace render::vfx
{
    void VFXSceneGPUPipeline::loadShader()
    {
        gpuShader = std::make_shared<core::Shader>(device);
        gpuShader->readShader("../../resources/shaders/vfx/vfx_billboard_gpu.glsl");

        if (gpuShader->getShaderStages().empty())
        {
            vfLogError("VFXSceneGPUPipeline: Failed to load shader: {}",
                        gpuShader->getLastCompilationError());
        }
    }

    void VFXSceneGPUPipeline::createDescriptorSetLayout()
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

        bindings[4].binding = 4;
        bindings[4].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void VFXSceneGPUPipeline::createDescriptorPool()
    {
        uint32_t totalSets = MAX_TEXTURE_SLOTS + 1;

        std::array<vk::DescriptorPoolSize, 3> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = totalSets;
        poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = totalSets * 2;  // particle texture + depth texture per set
        poolSizes[2].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[2].descriptorCount = totalSets * 2;

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

    void VFXSceneGPUPipeline::setLightingLayouts(
        vk::DescriptorSetLayout lightBuffer,
        vk::DescriptorSetLayout clusterGrid,
        vk::DescriptorSetLayout clusterLightGrid)
    {
        lightBufferLayout = lightBuffer;
        clusterGridLayout = clusterGrid;
        clusterLightGridLayout = clusterLightGrid;
    }

    void VFXSceneGPUPipeline::updateLightingDescriptorSets(
        vk::DescriptorSet lightBuffer,
        vk::DescriptorSet clusterGrid,
        vk::DescriptorSet clusterLightGrid)
    {
        cachedLightBufferSet = lightBuffer;
        cachedClusterGridSet = clusterGrid;
        cachedClusterLightGridSet = clusterLightGrid;
        lightingAvailable = lightBuffer && clusterGrid && clusterLightGrid;
    }

    void VFXSceneGPUPipeline::createPipeline()
    {
        auto vertexBinding = VFXQuadVertex::getBindingDescription();
        auto vertexAttribs = VFXQuadVertex::getAttributeDescriptions();

        std::vector<vk::DescriptorSetLayout> layouts = {descriptorSetLayout};
        if (lightBufferLayout && clusterGridLayout && clusterLightGridLayout)
        {
            layouts.push_back(lightBufferLayout);
            layouts.push_back(clusterGridLayout);
            layouts.push_back(clusterLightGridLayout);
        }

        core::GraphicsPipelineConfig config{
            .device = device.getLogicalDevice(),
            .renderPass = externalRenderPass,
            .extent = swapChain.getSwapchainExtent(),
            .shaderStages = gpuShader->getShaderStages(),
            .vertexBindings = {vertexBinding},
            .vertexAttributes = {vertexAttribs.begin(), vertexAttribs.end()},
            .topology = vk::PrimitiveTopology::eTriangleList,
            .descriptorSetLayouts = layouts,
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

    void VFXSceneGPUPipeline::createBuffers()
    {
        auto vkDevice = device.getLogicalDevice();

        core::BufferInfoRequest uboRequest(vkDevice, device.getPhysicalDevice());
        uboRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        uboRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
        uboRequest.size = sizeof(GPUVFXCameraUBO);
        core::BufferUtilities::createBuffer(uboRequest, cameraUBO, cameraUBOAllocation, device.getMemoryManager());
        cameraUBOMapped = cameraUBOAllocation.mappedPtr;

        constexpr vk::DeviceSize vertexBufferSize = sizeof(VFXQuadVertex) * QUAD_VERTICES.size();
        core::BufferInfoRequest vertexRequest(vkDevice, device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer |
                              vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, quadVertexBuffer, quadVertexBufferAllocation, device.getMemoryManager());

        constexpr vk::DeviceSize indexBufferSize = sizeof(uint16_t) * QUAD_INDICES.size();
        core::BufferInfoRequest indexRequest(vkDevice, device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer |
                             vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, quadIndexBuffer, quadIndexBufferAllocation, device.getMemoryManager());

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
        core::ImageUtilities::createImage(imageInfo, defaultTextureImage, defaultTextureAllocation, device.getMemoryManager());

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

    void VFXSceneGPUPipeline::createSampler()
    {
        textureSampler = core::ImageUtilities::createVFXSampler(device.getLogicalDevice());
    }

    void VFXSceneGPUPipeline::createDepthSampler()
    {
        depthSampler = core::ImageUtilities::createVFXDepthSampler(device.getLogicalDevice());
    }
}
