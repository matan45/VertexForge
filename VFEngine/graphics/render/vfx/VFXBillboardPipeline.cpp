#include "VFXBillboardPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Texture.hpp"

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
        createRenderPass();
        createDescriptorSetLayout();
        createDescriptorPool();
        createBuffers();
        createDefaultTexture();
        createSampler();
        createDescriptorSet();
        createPipeline();
        createFramebuffers();

        initialized = true;
    }

    void VFXBillboardPipeline::loadShader()
    {
        vfxShader = std::make_shared<core::Shader>(device);
        vfxShader->readShader("../../resources/shaders/vfx/vfx_billboard.glsl");
    }

    void VFXBillboardPipeline::recreate()
    {
        for (auto& framebuffer : framebuffers)
        {
            device.getLogicalDevice().destroyFramebuffer(framebuffer);
        }
        device.getLogicalDevice().destroyRenderPass(renderPass);
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);

        createRenderPass();
        createPipeline();
        createFramebuffers();
    }

    void VFXBillboardPipeline::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        for (auto& framebuffer : framebuffers)
        {
            dev.destroyFramebuffer(framebuffer);
        }
        framebuffers.clear();

        if (graphicsPipeline) dev.destroyPipeline(graphicsPipeline);
        if (pipelineLayout) dev.destroyPipelineLayout(pipelineLayout);

        if (descriptorPool)
        {
            if (descriptorSet)
                dev.freeDescriptorSets(descriptorPool, descriptorSet);
            dev.destroyDescriptorPool(descriptorPool);
        }
        if (descriptorSetLayout) dev.destroyDescriptorSetLayout(descriptorSetLayout);

        if (renderPass) dev.destroyRenderPass(renderPass);

        if (cameraUBO)
        {
            dev.destroyBuffer(cameraUBO);
            dev.freeMemory(cameraUBOMemory);
            cameraUBO = nullptr;
        }
        if (quadVertexBuffer)
        {
            dev.destroyBuffer(quadVertexBuffer);
            dev.freeMemory(quadVertexBufferMemory);
            quadVertexBuffer = nullptr;
        }
        if (quadIndexBuffer)
        {
            dev.destroyBuffer(quadIndexBuffer);
            dev.freeMemory(quadIndexBufferMemory);
            quadIndexBuffer = nullptr;
        }
        if (instanceBuffer)
        {
            dev.destroyBuffer(instanceBuffer);
            dev.freeMemory(instanceBufferMemory);
            instanceBuffer = nullptr;
        }

        customTexture.reset();
        currentTexturePath.clear();

        if (textureSampler) dev.destroySampler(textureSampler);
        if (defaultTextureImageView) dev.destroyImageView(defaultTextureImageView);
        if (defaultTextureImage)
        {
            dev.destroyImage(defaultTextureImage);
            dev.freeMemory(defaultTextureMemory);
        }

        if (vfxShader)
        {
            vfxShader->cleanUp();
            vfxShader.reset();
        }

        currentInstanceCount = 0;
        initialized = false;
    }
}
