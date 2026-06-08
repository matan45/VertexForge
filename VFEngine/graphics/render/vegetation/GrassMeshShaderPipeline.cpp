#include "GrassMeshShaderPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "../../core/VulkanContext.hpp"
#include "../../core/SwapChain.hpp"
#include "print/Log.hpp"
#include <array>

namespace render::vegetation
{
    GrassMeshShaderPipeline::GrassMeshShaderPipeline() = default;

    GrassMeshShaderPipeline::~GrassMeshShaderPipeline()
    {
        cleanup();
    }

    void GrassMeshShaderPipeline::init(core::Device& device,
                                        vk::DescriptorSetLayout windLayout,
                                        vk::DescriptorSetLayout lightLayout,
                                        vk::DescriptorSetLayout bindlessLayout,
                                        const std::vector<vk::Format>& colorFormats, vk::Format depthFormat)
    {
        if (initialized) return;

        devicePtr = &device;

        createGrassDataDescriptor();
        createCameraDescriptor();
        createGrassPipeline(windLayout, lightLayout, bindlessLayout, colorFormats, depthFormat);

        if (graphicsPipeline)
        {
            initialized = true;
            vfLogInfo("GrassMeshShaderPipeline: Initialized successfully");
        }
        else
        {
            vfLogError("GrassMeshShaderPipeline: Failed to initialize - pipeline creation failed");
        }
    }

    void GrassMeshShaderPipeline::cleanup()
    {
        if (!initialized || !devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        if (grassShader)
        {
            grassShader->cleanUp();
            grassShader.reset();
        }

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

        if (grassDataPool)
        {
            vkDevice.destroyDescriptorPool(grassDataPool);
            grassDataPool = nullptr;
        }

        if (grassDataLayout)
        {
            vkDevice.destroyDescriptorSetLayout(grassDataLayout);
            grassDataLayout = nullptr;
        }

        if (cameraPool)
        {
            vkDevice.destroyDescriptorPool(cameraPool);
            cameraPool = nullptr;
        }

        if (cameraLayout)
        {
            vkDevice.destroyDescriptorSetLayout(cameraLayout);
            cameraLayout = nullptr;
        }

        initialized = false;
        devicePtr = nullptr;
    }

    void GrassMeshShaderPipeline::recreate(vk::DescriptorSetLayout windLayout,
                                            vk::DescriptorSetLayout lightLayout,
                                            vk::DescriptorSetLayout bindlessLayout,
                                            const std::vector<vk::Format>& colorFormats, vk::Format depthFormat)
    {
        if (!initialized || !devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();
        vkDevice.waitIdle();

        if (grassShader)
        {
            grassShader->cleanUp();
            grassShader.reset();
        }

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

        createGrassPipeline(windLayout, lightLayout, bindlessLayout, colorFormats, depthFormat);

        vfLogInfo("GrassMeshShaderPipeline: Recreated pipeline");
    }

    void GrassMeshShaderPipeline::updateGrassDataDescriptors(vk::Buffer grassInstanceBuffer,
                                                              vk::Buffer grassCountBuffer)
    {
        if (!initialized || !devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        std::array<vk::DescriptorBufferInfo, 2> bufferInfos{};
        bufferInfos[0].buffer = grassInstanceBuffer;
        bufferInfos[0].offset = 0;
        bufferInfos[0].range = VK_WHOLE_SIZE;

        bufferInfos[1].buffer = grassCountBuffer;
        bufferInfos[1].offset = 0;
        bufferInfos[1].range = VK_WHOLE_SIZE;

        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0].dstSet = grassDataDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[0].pBufferInfo = &bufferInfos[0];

        writes[1].dstSet = grassDataDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].pBufferInfo = &bufferInfos[1];

        vkDevice.updateDescriptorSets(writes, {});
    }

    void GrassMeshShaderPipeline::updateCameraDescriptor(vk::Buffer cameraBuffer)
    {
        if (!initialized || !devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = cameraBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet write{};
        write.dstSet = cameraDescriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eUniformBuffer;
        write.pBufferInfo = &bufferInfo;

        vkDevice.updateDescriptorSets(1, &write, 0, nullptr);
    }

    void GrassMeshShaderPipeline::updateSharedDescriptors(vk::DescriptorSet windDescSet,
                                                           vk::DescriptorSet lightDescSet,
                                                           vk::DescriptorSet bindlessDescSet)
    {
        if (!initialized) return;

        windDescriptorSet = windDescSet;
        lightDataDescriptorSet = lightDescSet;
        bindlessDescriptorSet = bindlessDescSet;
    }

    void GrassMeshShaderPipeline::bindDescriptorSets(vk::CommandBuffer cmd)
    {
        if (lightDataDescriptorSet && bindlessDescriptorSet)
        {
            std::array<vk::DescriptorSet, 5> sets = {
                grassDataDescriptorSet, cameraDescriptorSet, windDescriptorSet,
                lightDataDescriptorSet, bindlessDescriptorSet
            };
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, sets, {});
        }
        else if (lightDataDescriptorSet)
        {
            std::array<vk::DescriptorSet, 4> sets = {
                grassDataDescriptorSet, cameraDescriptorSet, windDescriptorSet,
                lightDataDescriptorSet
            };
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, sets, {});
        }
        else
        {
            std::array<vk::DescriptorSet, 3> sets = {
                grassDataDescriptorSet, cameraDescriptorSet, windDescriptorSet
            };
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, sets, {});
        }
    }

    void GrassMeshShaderPipeline::dispatch(vk::CommandBuffer cmd, const GrassDispatchParams& params)
    {
        if (!initialized || params.instanceCount == 0 || !graphicsPipeline) return;
        if (!grassDataDescriptorSet || !cameraDescriptorSet || !windDescriptorSet) return;

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        bindDescriptorSets(cmd);

        GrassMeshPushConstants pc{params.baseColor, params.tipColor, params.fadeStartDistance,
                                   params.fadeEndDistance, params.sssDistortion, params.sssPower,
                                   params.sssScale, params.billboardTextureIndex};
        cmd.pushConstants(pipelineLayout,
                          vk::ShaderStageFlagBits::eTaskEXT |
                          vk::ShaderStageFlagBits::eMeshEXT |
                          vk::ShaderStageFlagBits::eFragment,
                          0, sizeof(GrassMeshPushConstants), &pc);

        uint32_t taskGroups = (params.instanceCount + 31) / 32;
        cmd.drawMeshTasksEXT(taskGroups, 1, 1);
    }

    void GrassMeshShaderPipeline::createGrassDataDescriptor()
    {
        vk::Device vkDevice = devicePtr->getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eTaskEXT;

        grassDataLayout = core::PipelineUtilities::createUpdateAfterBindLayout(
            vkDevice, bindings.data(), static_cast<uint32_t>(bindings.size()));

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 2;

        grassDataPool = core::PipelineUtilities::createUpdateAfterBindPool(
            vkDevice, 1, &poolSize, 1);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = grassDataPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &grassDataLayout;
        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        grassDataDescriptorSet = sets[0];
    }

    void GrassMeshShaderPipeline::createCameraDescriptor()
    {
        vk::Device vkDevice = devicePtr->getLogicalDevice();

        vk::DescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = vk::DescriptorType::eUniformBuffer;
        binding.descriptorCount = 1;
        binding.stageFlags = vk::ShaderStageFlagBits::eTaskEXT |
                             vk::ShaderStageFlagBits::eMeshEXT |
                             vk::ShaderStageFlagBits::eFragment;

        cameraLayout = core::PipelineUtilities::createUpdateAfterBindLayout(vkDevice, &binding, 1);

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eUniformBuffer;
        poolSize.descriptorCount = 1;

        cameraPool = core::PipelineUtilities::createUpdateAfterBindPool(vkDevice, 1, &poolSize, 1);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = cameraPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &cameraLayout;
        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        cameraDescriptorSet = sets[0];
    }

    void GrassMeshShaderPipeline::createPipelineLayout(vk::DescriptorSetLayout windLayout,
                                                        vk::DescriptorSetLayout lightLayout,
                                                        vk::DescriptorSetLayout bindlessLayout)
    {
        vk::Device vkDevice = devicePtr->getLogicalDevice();

        this->windLayout = windLayout;
        this->lightDataLayout = lightLayout;
        this->bindlessLayout = bindlessLayout;

        std::array<vk::DescriptorSetLayout, 5> setLayouts = {
            grassDataLayout, cameraLayout, windLayout, lightLayout, bindlessLayout
        };

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eTaskEXT |
                               vk::ShaderStageFlagBits::eMeshEXT |
                               vk::ShaderStageFlagBits::eFragment;
        pushRange.offset = 0;
        pushRange.size = sizeof(GrassMeshPushConstants);

        vk::PipelineLayoutCreateInfo layoutCreateInfo{};
        layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutCreateInfo.pSetLayouts = setLayouts.data();
        layoutCreateInfo.pushConstantRangeCount = 1;
        layoutCreateInfo.pPushConstantRanges = &pushRange;

        pipelineLayout = vkDevice.createPipelineLayout(layoutCreateInfo);
    }

    void GrassMeshShaderPipeline::createGrassPipeline(vk::DescriptorSetLayout windLayout,
                                                       vk::DescriptorSetLayout lightLayout,
                                                       vk::DescriptorSetLayout bindlessLayout,
                                                       const std::vector<vk::Format>& colorFormats, vk::Format depthFormat)
    {
        if (!loadGrassShaders()) return;

        createPipelineLayout(windLayout, lightLayout, bindlessLayout);

        core::MeshShaderPipelineConfig config{};
        config.device = devicePtr->getLogicalDevice();
        config.extent = vk::Extent2D{1, 1};
        config.colorAttachmentFormats = colorFormats;
        config.depthAttachmentFormat = depthFormat;
        config.shaderStages = grassShader->getShaderStages();
        config.existingPipelineLayout = pipelineLayout;
        config.cullMode = vk::CullModeFlagBits::eNone;
        config.depthTestEnable = true;
        config.depthWriteEnable = true;
        config.blendEnable = true;
        config.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        config.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        config.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        config.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        config.dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        config.sampleCount = core::VulkanContext::getSwapChain()->getMSAASamples();

        try
        {
            auto result = core::PipelineUtilities::createMeshShaderPipeline(config);
            graphicsPipeline = result.pipeline;
        }
        catch (const std::exception& e)
        {
            vfLogError("GrassMeshShaderPipeline: Failed to create pipeline - {}", e.what());
        }
    }

    bool GrassMeshShaderPipeline::loadGrassShaders()
    {
        grassShader = std::make_unique<core::Shader>(*devicePtr);
        grassShader->readShader("../../resources/shaders/vegetation/task_grass.glsl");
        grassShader->readShader("../../resources/shaders/vegetation/mesh_grass.glsl");
        grassShader->readShader("../../resources/shaders/vegetation/frag_grass.glsl");

        const auto& stages = grassShader->getShaderStages();
        if (stages.size() < 3)
        {
            vfLogError("GrassMeshShaderPipeline: Failed to load shaders (need Task + Mesh + Fragment): {}",
                        grassShader->getLastCompilationError());
            return false;
        }

        bool hasTask = false, hasMesh = false, hasFrag = false;
        for (const auto& stage : stages)
        {
            if (stage.stage == vk::ShaderStageFlagBits::eTaskEXT) hasTask = true;
            if (stage.stage == vk::ShaderStageFlagBits::eMeshEXT) hasMesh = true;
            if (stage.stage == vk::ShaderStageFlagBits::eFragment) hasFrag = true;
        }

        if (!hasTask || !hasMesh || !hasFrag)
        {
            vfLogError("GrassMeshShaderPipeline: Missing shader stages (Task={}, Mesh={}, Fragment={})",
                        hasTask, hasMesh, hasFrag);
            return false;
        }

        return true;
    }
}
