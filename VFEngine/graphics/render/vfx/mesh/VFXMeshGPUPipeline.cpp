#include "VFXMeshGPUPipeline.hpp"
#include "../../mesh/MeshTypes.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/Texture.hpp"
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

    void VFXMeshGPUPipeline::init(vk::Format colorFmt, vk::Format depthFmt)
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
                vfLogError("VFXMeshGPUPipeline: Failed to load shader");
                return;
            }

            createDescriptorSetLayout();
            if (!descriptorSetLayout)
            {
                vfLogError("VFXMeshGPUPipeline: Failed to create descriptor set layout");
                return;
            }

            createDescriptorPool();
            if (!descriptorPool)
            {
                vfLogError("VFXMeshGPUPipeline: Failed to create descriptor pool");
                cleanup();
                return;
            }

            createBuffers();
            if (!cameraUBO)
            {
                vfLogError("VFXMeshGPUPipeline: Failed to create buffers");
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
                vfLogError("VFXMeshGPUPipeline: Failed to create graphics pipeline");
                cleanup();
                return;
            }

            initialized = true;
        }
        catch (const vk::SystemError& e)
        {
            vfLogError("VFXMeshGPUPipeline: Vulkan error during init - {}", e.what());
            cleanup();
        }
        catch (const std::exception& e)
        {
            vfLogError("VFXMeshGPUPipeline: Exception during init - {}", e.what());
            cleanup();
        }
    }

    void VFXMeshGPUPipeline::recreate(vk::Format colorFmt, vk::Format depthFmt)
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

    void VFXMeshGPUPipeline::cleanup()
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

        if (emptySetLayout)
        {
            vkDevice.destroyDescriptorSetLayout(emptySetLayout);
            emptySetLayout = nullptr;
        }

        cameraUBOMapped = nullptr;
        core::BufferUtilities::destroyBuffer(vkDevice, cameraUBO, cameraUBOAllocation, device.getMemoryManager());

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

    }

    void VFXMeshGPUPipeline::loadShader()
    {
        gpuShader = std::make_shared<core::Shader>(device);
        gpuShader->readShader("../../resources/shaders/vfx/vfx_mesh_particle.glsl");

        if (gpuShader->getShaderStages().empty())
        {
            vfLogError("VFXMeshGPUPipeline: Failed to load shader: {}",
                        gpuShader->getLastCompilationError());
        }
    }

    void VFXMeshGPUPipeline::createDescriptorSetLayout()
    {
        // VK-1481: binding 1 (per-emitter texture) removed — textures now live in the shared
        // bindless set (set 4). The numeric gap at binding 1 is legal.
        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};

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

        // Binding 4: Scene depth (soft particles)
        bindings[3].binding = 4;
        bindings[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);

        // VK-1481: empty 0-binding layout used to pad missing lighting sets so bindless is always set 4.
        vk::DescriptorSetLayoutCreateInfo emptyLayoutInfo{};
        emptySetLayout = device.getLogicalDevice().createDescriptorSetLayout(emptyLayoutInfo);
    }

    void VFXMeshGPUPipeline::createDescriptorPool()
    {
        // VK-1481: a single set-0 (no per-texture cloning). Depth is the only combined-image-sampler.
        std::array<vk::DescriptorPoolSize, 3> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = 1;                      // camera UBO
        poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = 1;                      // scene depth
        poolSizes[2].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[2].descriptorCount = 2;                      // particle + config SSBO

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void VFXMeshGPUPipeline::allocateDescriptorSet()
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        defaultDescriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
    }

    void VFXMeshGPUPipeline::setLightingLayouts(
        vk::DescriptorSetLayout lightBuffer,
        vk::DescriptorSetLayout clusterGrid,
        vk::DescriptorSetLayout clusterLightGrid)
    {
        lightBufferLayout = lightBuffer;
        clusterGridLayout = clusterGrid;
        clusterLightGridLayout = clusterLightGrid;
    }

    void VFXMeshGPUPipeline::updateLightingDescriptorSets(
        vk::DescriptorSet lightBuffer,
        vk::DescriptorSet clusterGrid,
        vk::DescriptorSet clusterLightGrid)
    {
        cachedLightBufferSet = lightBuffer;
        cachedClusterGridSet = clusterGrid;
        cachedClusterLightGridSet = clusterLightGrid;
        lightingAvailable = lightBuffer && clusterGrid && clusterLightGrid;
    }

    void VFXMeshGPUPipeline::createPipeline()
    {
        auto vertexBinding = render::mesh::MeshVertexInput::getBindingDescription();
        auto vertexAttribs = render::mesh::MeshVertexInput::getAttributeDescriptions();

        // VK-1481: sets 1-3 are the lighting sets; the shared bindless texture set is at set 4. When
        // lighting layouts are absent (VFX GPU init ran before they were cached — GPU-driven off), pad
        // sets 1-3 with an empty layout so bindless ALWAYS lands at set 4, matching the shaders.
        std::vector<vk::DescriptorSetLayout> layouts = {descriptorSetLayout};
        layouts.push_back(lightBufferLayout ? lightBufferLayout : emptySetLayout);
        layouts.push_back(clusterGridLayout ? clusterGridLayout : emptySetLayout);
        layouts.push_back(clusterLightGridLayout ? clusterLightGridLayout : emptySetLayout);

        assert(bindless && "VFXMeshGPUPipeline: bindless table must be set before init()");
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
            .pushConstantSize = sizeof(GPUVFXBillboardPushConstants),
            .pushConstantStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .cullMode = vk::CullModeFlagBits::eBack,
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

    void VFXMeshGPUPipeline::createBuffers()
    {
        auto vkDevice = device.getLogicalDevice();

        core::BufferInfoRequest uboRequest(vkDevice, device.getPhysicalDevice());
        uboRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        uboRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
        uboRequest.size = sizeof(GPUVFXCameraUBO);
        core::BufferUtilities::createBuffer(uboRequest, cameraUBO, cameraUBOAllocation, device.getMemoryManager());
        cameraUBOMapped = cameraUBOAllocation.mappedPtr;
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

    void VFXMeshGPUPipeline::createSampler()
    {
        textureSampler = core::ImageUtilities::createVFXSampler(device.getLogicalDevice());
    }

    void VFXMeshGPUPipeline::createDepthSampler()
    {
        depthSampler = core::ImageUtilities::createVFXDepthSampler(device.getLogicalDevice());
    }
}
