#include "VFXSceneGPUPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Texture.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"

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
                vfLogError("VFXSceneGPUPipeline: Failed to load shader");
                return;
            }

            createDescriptorSetLayout();
            if (!descriptorSetLayout)
            {
                vfLogError("VFXSceneGPUPipeline: Failed to create descriptor set layout");
                return;
            }

            createDescriptorPool();
            if (!descriptorPool)
            {
                vfLogError("VFXSceneGPUPipeline: Failed to create descriptor pool");
                cleanup();
                return;
            }

            createBuffers();
            if (!cameraUBO || !quadVertexBuffer || !quadIndexBuffer)
            {
                vfLogError("VFXSceneGPUPipeline: Failed to create buffers");
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
                vfLogError("VFXSceneGPUPipeline: Failed to create graphics pipeline");
                cleanup();
                return;
            }

            initialized = true;
        }
        catch (const vk::SystemError& e)
        {
            vfLogError("VFXSceneGPUPipeline: Vulkan error during init - {}", e.what());
            cleanup();
        }
        catch (const std::exception& e)
        {
            vfLogError("VFXSceneGPUPipeline: Exception during init - {}", e.what());
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
        sceneDepthImageView = nullptr;
        descriptorsNeedUpdate = true;
        initialized = false;

    }
}
