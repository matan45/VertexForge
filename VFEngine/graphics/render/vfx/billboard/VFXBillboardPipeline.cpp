#include "VFXBillboardPipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/Texture.hpp"
#include "../../../core/BufferUtilities.hpp"

namespace render::vfx
{
    VFXBillboardPipeline::VFXBillboardPipeline(core::Device& device, core::SwapChain& swapChain,
                                               core::OffscreenResources& offscreenResources)
        : device{device}
        , swapChain{swapChain}
        , offscreenResources{offscreenResources}
    {
    }

    VFXBillboardPipeline::~VFXBillboardPipeline() = default;

    void VFXBillboardPipeline::init()
    {
        loadShader();
        createDescriptorSetLayout();
        createDescriptorPool();
        createBuffers();
        createDefaultTexture();
        createSampler();
        createDescriptorSet();
        createPipeline();

        initialized = true;
    }

    void VFXBillboardPipeline::loadShader()
    {
        vfxShader = std::make_shared<core::Shader>(device);
        vfxShader->readShader("../../resources/shaders/vfx/vfx_billboard.glsl");
    }

    void VFXBillboardPipeline::recreate()
    {
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);

        createPipeline();
    }

    void VFXBillboardPipeline::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        if (graphicsPipeline) { dev.destroyPipeline(graphicsPipeline); graphicsPipeline = nullptr; }
        if (pipelineLayout) { dev.destroyPipelineLayout(pipelineLayout); pipelineLayout = nullptr; }

        if (descriptorPool)
        {
            if (descriptorSet)
            {
                dev.freeDescriptorSets(descriptorPool, descriptorSet);
                descriptorSet = nullptr;
            }
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (descriptorSetLayout) { dev.destroyDescriptorSetLayout(descriptorSetLayout); descriptorSetLayout = nullptr; }

        core::BufferUtilities::destroyBuffer(dev, cameraUBO, cameraUBOAllocation, device.getMemoryManager());
        core::BufferUtilities::destroyBuffer(dev, quadVertexBuffer, quadVertexBufferAllocation, device.getMemoryManager());
        core::BufferUtilities::destroyBuffer(dev, quadIndexBuffer, quadIndexBufferAllocation, device.getMemoryManager());
        core::BufferUtilities::destroyBuffer(dev, instanceBuffer, instanceBufferAllocation, device.getMemoryManager());

        customTexture.reset();
        currentTexturePath.clear();

        if (textureSampler) { dev.destroySampler(textureSampler); textureSampler = nullptr; }
        if (defaultTextureImageView) { dev.destroyImageView(defaultTextureImageView); defaultTextureImageView = nullptr; }
        if (defaultTextureImage)
        {
            dev.destroyImage(defaultTextureImage);
            defaultTextureImage = nullptr;
        }
        if (defaultTextureAllocation) { device.getMemoryManager().free(defaultTextureAllocation); defaultTextureAllocation = {}; }

        if (vfxShader)
        {
            vfxShader->cleanUp();
            vfxShader.reset();
        }

        currentInstanceCount = 0;
        initialized = false;
    }
}
