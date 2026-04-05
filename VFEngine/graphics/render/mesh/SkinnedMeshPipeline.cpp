#include "SkinnedMeshPipeline.hpp"
#include "../ibl/DefaultIBLTextureFactory.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"
#include <stdexcept>

namespace render::mesh
{
    SkinnedMeshPipeline::SkinnedMeshPipeline(core::Device& device, core::SwapChain& swapChain,
                                             core::OffscreenResources& offscreenResources)
        : device{device}, swapChain{swapChain}, offscreenResources{offscreenResources}
    {
    }

    SkinnedMeshPipeline::~SkinnedMeshPipeline()
    {
        cleanUp();
    }

    void SkinnedMeshPipeline::init()
    {
        loadShaders();
        createDescriptorSetLayouts();
        createDescriptorPools();
        createCameraUBO();
        createBoneSSBO();

        defaultIBLFactory = std::make_unique<ibl::DefaultIBLTextureFactory>(device);
        defaultIBLFactory->createDefaultTextures(device.getStagingCommandPool());

        createDescriptorSets();
        createPipelineLayout();
        createGraphicsPipeline();
        usingDefaultTextures = true;
    }

    void SkinnedMeshPipeline::loadShaders()
    {
        skinnedMeshShader = std::make_shared<core::Shader>(device);
        skinnedMeshShader->readShader("../../resources/shaders/mesh/skinned_mesh.glsl");

        if (skinnedMeshShader->getShaderStages().empty())
        {
            throw std::runtime_error("Failed to load skinned_mesh.glsl shader");
        }
    }

    void SkinnedMeshPipeline::cleanUp()
    {
        auto logicalDevice = device.getLogicalDevice();

        logicalDevice.waitIdle();

        destroyMeshGPUBuffers();
        loadedMesh.reset();

        if (graphicsPipeline)
        {
            logicalDevice.destroyPipeline(graphicsPipeline);
            graphicsPipeline = nullptr;
        }
        if (pipelineLayout)
        {
            logicalDevice.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        if (cameraUBO && !externalCameraBuffer)
        {
            logicalDevice.destroyBuffer(cameraUBO);
            cameraUBO = nullptr;
        }
        if (!externalCameraBuffer)
        {
            device.getMemoryManager().free(cameraUBOAllocation);
            cameraUBOAllocation = {};
        }

        boneSSBOMapped = nullptr;
        if (boneSSBO)
        {
            logicalDevice.destroyBuffer(boneSSBO);
            boneSSBO = nullptr;
        }
        device.getMemoryManager().free(boneSSBOAllocation);
        boneSSBOAllocation = {};

        if (cameraIBLDescriptorPool)
        {
            logicalDevice.destroyDescriptorPool(cameraIBLDescriptorPool);
            cameraIBLDescriptorPool = nullptr;
        }
        if (textureDescriptorPool)
        {
            logicalDevice.destroyDescriptorPool(textureDescriptorPool);
            textureDescriptorPool = nullptr;
        }
        if (boneDescriptorPool)
        {
            logicalDevice.destroyDescriptorPool(boneDescriptorPool);
            boneDescriptorPool = nullptr;
        }

        if (cameraIBLDescriptorSetLayout)
        {
            logicalDevice.destroyDescriptorSetLayout(cameraIBLDescriptorSetLayout);
            cameraIBLDescriptorSetLayout = nullptr;
        }
        if (textureDescriptorSetLayout)
        {
            logicalDevice.destroyDescriptorSetLayout(textureDescriptorSetLayout);
            textureDescriptorSetLayout = nullptr;
        }
        if (boneDescriptorSetLayout)
        {
            logicalDevice.destroyDescriptorSetLayout(boneDescriptorSetLayout);
            boneDescriptorSetLayout = nullptr;
        }

        cameraIBLDescriptorSet = nullptr;
        textureDescriptorSet = nullptr;
        boneDescriptorSet = nullptr;

        defaultIBLFactory.reset();

        if (skinnedMeshShader)
        {
            skinnedMeshShader->cleanUp();
            skinnedMeshShader.reset();
        }
    }
}
