#include "VFXRibbonGPUPipeline.hpp"
#include "../quad/VFXQuadData.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/ImageUtilities.hpp"
#include "../../../core/DeferredDeletionQueue.hpp"
#include "../bindless/VFXBindlessTextures.hpp"
#include "print/Log.hpp"
#include "../compute/GPUVFXTypes.hpp"

#include <cassert>


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

    void VFXRibbonGPUPipeline::init(vk::Format colorFmt, vk::Format depthFmt)
    {
        if (initialized)
        {
            return;
        }

        colorFormat = colorFmt;
        depthFormat = depthFmt;

        try
        {
            loadShader();
            if (!gpuShader)
            {
                vfLogError("VFXRibbonGPUPipeline: Failed to load shader");
                return;
            }

            createDescriptorSetLayout();
            if (!descriptorSetLayout)
            {
                vfLogError("VFXRibbonGPUPipeline: Failed to create descriptor set layout");
                return;
            }

            createDescriptorPool();
            if (!descriptorPool)
            {
                vfLogError("VFXRibbonGPUPipeline: Failed to create descriptor pool");
                cleanup();
                return;
            }

            createBuffers();
            if (!cameraUBO)
            {
                vfLogError("VFXRibbonGPUPipeline: Failed to create buffers");
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
                vfLogError("VFXRibbonGPUPipeline: Failed to create graphics pipeline");
                cleanup();
                return;
            }

            initialized = true;
        }
        catch (const vk::SystemError& e)
        {
            vfLogError("VFXRibbonGPUPipeline: Vulkan error during init - {}", e.what());
            cleanup();
        }
        catch (const std::exception& e)
        {
            vfLogError("VFXRibbonGPUPipeline: Exception during init - {}", e.what());
            cleanup();
        }
    }

    void VFXRibbonGPUPipeline::recreate(vk::Format colorFmt, vk::Format depthFmt)
    {
        if (!initialized)
        {
            return;
        }

        colorFormat = colorFmt;
        depthFormat = depthFmt;

        auto vkDevice = device.getLogicalDevice();
        vkDevice.destroyPipeline(graphicsPipeline);
        if (multiplyPipeline) { vkDevice.destroyPipeline(multiplyPipeline); multiplyPipeline = nullptr; } // VK-1472
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

        if (multiplyPipeline) // VK-1472
        {
            vkDevice.destroyPipeline(multiplyPipeline);
            multiplyPipeline = nullptr;
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

        cameraUBOMapped = nullptr;
        renderDataMapped = nullptr;
        core::BufferUtilities::destroyBuffer(vkDevice, cameraUBO, cameraUBOAllocation, device.getMemoryManager());
        core::BufferUtilities::destroyBuffer(vkDevice, renderDataBuffer, renderDataBufferAllocation, device.getMemoryManager());
        core::BufferUtilities::destroyBuffer(vkDevice, quadVertexBuffer, quadVertexBufferAllocation, device.getMemoryManager());
        core::BufferUtilities::destroyBuffer(vkDevice, quadIndexBuffer, quadIndexBufferAllocation, device.getMemoryManager());

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
            defaultTextureImage = nullptr;
        }
        if (defaultTextureAllocation) { device.getMemoryManager().free(defaultTextureAllocation); defaultTextureAllocation = {}; }

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

    }

    void VFXRibbonGPUPipeline::loadShader()
    {
        gpuShader = std::make_shared<core::Shader>(device);
        gpuShader->readShader("../../resources/shaders/vfx/vfx_ribbon_gpu.glsl");

        if (gpuShader->getShaderStages().empty())
        {
            vfLogError("VFXRibbonGPUPipeline: Failed to load shader: {}",
                        gpuShader->getLastCompilationError());
        }
    }

    void VFXRibbonGPUPipeline::createDescriptorSetLayout()
    {
        // VK-1481: binding 1 (per-emitter texture) removed — textures now live in the shared
        // bindless set (set 4). The numeric gap at binding 1 is legal. The ribbon-specific
        // ring/head/LUT SSBOs (bindings 5/6/7) are preserved.
        std::array<vk::DescriptorSetLayoutBinding, 8> bindings{};

        // Binding 0: Camera UBO
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

        // Binding 2: Particle SSBO
        bindings[1].binding = 2;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eVertex;

        // Binding 3: Emitter config SSBO
        bindings[2].binding = 3;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

        // Binding 4: Scene depth texture (soft particles)
        bindings[3].binding = 4;
        bindings[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eFragment;

        // Binding 5: ribbon ring SSBO
        bindings[4].binding = 5;
        bindings[4].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = vk::ShaderStageFlagBits::eVertex;

        // Binding 6: ribbon head SSBO
        bindings[5].binding = 6;
        bindings[5].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags = vk::ShaderStageFlagBits::eVertex;

        // Binding 7: baked LUT SSBO (VK-1474: ribbon width curve ch7 / tail gradient ch8)
        bindings[6].binding = 7;
        bindings[6].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[6].descriptorCount = 1;
        bindings[6].stageFlags = vk::ShaderStageFlagBits::eVertex;

        // VK-1481 Phase 2, Binding 8: per-emitter render-data SSBO (merged draw reads by emitterSlot)
        bindings[7].binding = 8;
        bindings[7].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[7].descriptorCount = 1;
        bindings[7].stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void VFXRibbonGPUPipeline::createDescriptorPool()
    {
        // VK-1481: a single set-0 (no per-texture cloning). Depth is the only combined-image-sampler
        // (the per-emitter texture binding is gone). Storage buffers in set 0: particle + config +
        // ribbon ring + ribbon head + baked LUT + render-data.
        std::array<vk::DescriptorPoolSize, 3> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = 1;                      // camera UBO
        poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = 1;                      // scene depth
        poolSizes[2].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[2].descriptorCount = 6;                      // particle + config + ring + head + LUT + render-data

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void VFXRibbonGPUPipeline::allocateDescriptorSet()
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        defaultDescriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
    }

    void VFXRibbonGPUPipeline::setLightingLayouts(
        vk::DescriptorSetLayout lightBuffer,
        vk::DescriptorSetLayout clusterGrid,
        vk::DescriptorSetLayout clusterLightGrid)
    {
        lightBufferLayout = lightBuffer;
        clusterGridLayout = clusterGrid;
        clusterLightGridLayout = clusterLightGrid;
    }

    void VFXRibbonGPUPipeline::updateLightingDescriptorSets(
        vk::DescriptorSet lightBuffer,
        vk::DescriptorSet clusterGrid,
        vk::DescriptorSet clusterLightGrid)
    {
        cachedLightBufferSet = lightBuffer;
        cachedClusterGridSet = clusterGrid;
        cachedClusterLightGridSet = clusterLightGrid;
        lightingAvailable = lightBuffer && clusterGrid && clusterLightGrid;
    }

    void VFXRibbonGPUPipeline::createPipeline()
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

        // VK-1481: shared bindless texture set at set index 4. The lit ribbon shader statically
        // references lighting sets 1-3, so those layouts are always present here (invariant asserted below).
        assert(bindless && "VFXRibbonGPUPipeline: bindless table must be set before init()");
        assert(layouts.size() == 4 && "VFX ribbon expects sets 0-3 before the bindless set 4");
        layouts.push_back(bindless->getDescriptorSetLayout());

        core::GraphicsPipelineConfig config{
            .device = device.getLogicalDevice(),
            .extent = swapChain.getSwapchainExtent(),
            .colorAttachmentFormats = {colorFormat},
            .depthAttachmentFormat = depthFormat,
            .shaderStages = gpuShader->getShaderStages(),
            .vertexBindings = {vertexBinding},
            .vertexAttributes = {vertexAttribs.begin(), vertexAttribs.end()},
            .topology = vk::PrimitiveTopology::eTriangleList,
            .descriptorSetLayouts = layouts,
            .pushConstantSize = sizeof(GPUVFXMergedPushConstants), // VK-1481 Phase 2: {runBaseSlot}
            .pushConstantStages = vk::ShaderStageFlagBits::eVertex,
            .cullMode = vk::CullModeFlagBits::eNone,
            .depthTestEnable = true,
            .depthWriteEnable = false,
            .blendEnable = true,
            .srcColorBlendFactor = vk::BlendFactor::eOne,            // VK-1472: premultiplied shared state
            .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha
        };

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;

        // VK-1472: Multiply blend variant (dst*src) reuses the shared layout.
        core::GraphicsPipelineConfig multiplyConfig = config;
        multiplyConfig.existingPipelineLayout = pipelineLayout;
        multiplyConfig.srcColorBlendFactor = vk::BlendFactor::eDstColor;
        multiplyConfig.dstColorBlendFactor = vk::BlendFactor::eZero;
        multiplyPipeline = core::PipelineUtilities::createGraphicsPipeline(multiplyConfig).pipeline;
    }

    void VFXRibbonGPUPipeline::createBuffers()
    {
        auto vkDevice = device.getLogicalDevice();

        core::BufferInfoRequest uboRequest(vkDevice, device.getPhysicalDevice());
        uboRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        uboRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
        uboRequest.size = sizeof(GPUVFXCameraUBO);
        core::BufferUtilities::createBuffer(uboRequest, cameraUBO, cameraUBOAllocation, device.getMemoryManager());
        cameraUBOMapped = cameraUBOAllocation.mappedPtr;

        // VK-1481 Phase 2: per-emitter render-data SSBO (host-visible, mapped, one slot per emitter).
        core::BufferInfoRequest renderDataRequest(vkDevice, device.getPhysicalDevice());
        renderDataRequest.usage = vk::BufferUsageFlagBits::eStorageBuffer;
        renderDataRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                       vk::MemoryPropertyFlagBits::eHostCoherent;
        renderDataRequest.size = sizeof(VFXEmitterRenderData) * GPUVFXConstants::MAX_EMITTERS;
        core::BufferUtilities::createBuffer(renderDataRequest, renderDataBuffer, renderDataBufferAllocation, device.getMemoryManager());
        renderDataMapped = renderDataBufferAllocation.mappedPtr;

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

    void VFXRibbonGPUPipeline::createSampler()
    {
        textureSampler = core::ImageUtilities::createVFXSampler(device.getLogicalDevice());
    }

    void VFXRibbonGPUPipeline::createDepthSampler()
    {
        depthSampler = core::ImageUtilities::createVFXDepthSampler(device.getLogicalDevice());
    }
}
