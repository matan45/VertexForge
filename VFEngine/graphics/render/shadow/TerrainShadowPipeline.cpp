#include "TerrainShadowPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "print/Log.hpp"
#include <array>

namespace render::shadow
{
    TerrainShadowPipeline::TerrainShadowPipeline(core::Device& device)
        : device(device)
    {
    }

    TerrainShadowPipeline::~TerrainShadowPipeline()
    {
        cleanup();
    }

    void TerrainShadowPipeline::init(vk::DescriptorSetLayout terrainDataLayout,
                                      vk::DescriptorSetLayout meshletDataLayout,
                                      vk::DescriptorSetLayout vertexDataLayout,
                                      vk::RenderPass shadowRenderPass)
    {
        if (initialized)
        {
            return;
        }

        cachedTerrainDataLayout = terrainDataLayout;
        cachedMeshletDataLayout = meshletDataLayout;
        cachedVertexDataLayout = vertexDataLayout;

        createTerrainShadowPipeline(shadowRenderPass);

        initialized = true;
    }

    void TerrainShadowPipeline::cleanup()
    {
        if (!initialized)
            return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (terrainShadowShader)
        {
            terrainShadowShader->cleanUp();
            terrainShadowShader.reset();
        }

        if (terrainShadowPipeline)
        {
            vkDevice.destroyPipeline(terrainShadowPipeline);
            terrainShadowPipeline = nullptr;
        }

        if (terrainShadowPipelineLayout)
        {
            vkDevice.destroyPipelineLayout(terrainShadowPipelineLayout);
            terrainShadowPipelineLayout = nullptr;
        }

        initialized = false;
    }

    void TerrainShadowPipeline::createTerrainShadowPipeline(vk::RenderPass shadowRenderPass)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        terrainShadowShader = std::make_unique<core::Shader>(device);
        terrainShadowShader->readShader("../../resources/shaders/shadow/shadow_terrain.glsl");

        const auto& stages = terrainShadowShader->getShaderStages();
        if (stages.size() < 2)
        {
            vfLogError("TerrainShadowPipeline: Failed to load terrain shadow shaders: {}",
                        terrainShadowShader->getLastCompilationError());
            return;
        }

        bool hasTask = false, hasMesh = false;
        for (const auto& stage : stages)
        {
            if (stage.stage == vk::ShaderStageFlagBits::eTaskEXT) hasTask = true;
            if (stage.stage == vk::ShaderStageFlagBits::eMeshEXT) hasMesh = true;
        }

        if (!hasTask || !hasMesh)
        {
            vfLogError("TerrainShadowPipeline: Missing shader stages (Task={}, Mesh={})", hasTask, hasMesh);
            return;
        }

        // Descriptor set layouts:
        // Set 0: Terrain tile data
        // Set 1: Meshlet data (meshlets, vertex indices, primitives)
        // Set 2: Vertex data
        std::array<vk::DescriptorSetLayout, 3> setLayouts = {
            cachedTerrainDataLayout,
            cachedMeshletDataLayout,
            cachedVertexDataLayout
        };

        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(TerrainShadowPushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        terrainShadowPipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        std::array<vk::DynamicState, 3> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor,
            vk::DynamicState::eDepthBias
        };

        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = nullptr;
        viewportState.scissorCount = 1;
        viewportState.pScissors = nullptr;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        // Use back-face culling for terrain shadows to enable proper self-shadowing
        // (terrain is viewed from above, back faces don't contribute to visible shadows)
        rasterizer.cullMode = vk::CullModeFlagBits::eBack;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizer.depthBiasEnable = VK_TRUE;
        rasterizer.depthBiasConstantFactor = 0.0f;
        rasterizer.depthBiasSlopeFactor = 0.0f;
        rasterizer.depthBiasClamp = 0.0f;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = vk::CompareOp::eLess;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 0;
        colorBlending.pAttachments = nullptr;

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = nullptr;
        pipelineInfo.pInputAssemblyState = nullptr;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = terrainShadowPipelineLayout;
        pipelineInfo.renderPass = shadowRenderPass;
        pipelineInfo.subpass = 0;

        auto result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            vfLogError("TerrainShadowPipeline: Failed to create pipeline");
            return;
        }

        terrainShadowPipeline = result.value;
    }

    void TerrainShadowPipeline::dispatch(vk::CommandBuffer cmd,
                                          vk::DescriptorSet terrainDataDescSet,
                                          vk::DescriptorSet meshletDescSet,
                                          vk::DescriptorSet vertexDescSet,
                                          const glm::mat4& lightViewProjection,
                                          uint32_t tileCount,
                                          uint32_t shadowLOD,
                                          float depthBias,
                                          float slopeBias,
                                          float normalBias)
    {
        if (!initialized || tileCount == 0)
            return;

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, terrainShadowPipeline);

        std::array<vk::DescriptorSet, 3> descriptorSets = {
            terrainDataDescSet,
            meshletDescSet,
            vertexDescSet
        };

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            terrainShadowPipelineLayout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0,
            nullptr
        );

        TerrainShadowPushConstants pushConstants{};
        pushConstants.lightViewProjection = lightViewProjection;
        pushConstants.tileCount = tileCount;
        pushConstants.shadowLOD = shadowLOD;
        pushConstants.depthBias = depthBias;
        pushConstants.slopeBias = slopeBias;
        pushConstants.normalBias = normalBias;

        cmd.pushConstants(
            terrainShadowPipelineLayout,
            vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT,
            0,
            sizeof(TerrainShadowPushConstants),
            &pushConstants
        );

        cmd.setDepthBias(depthBias, 0.0f, slopeBias);

        // One task workgroup per tile
        cmd.drawMeshTasksEXT(tileCount, 1, 1);
    }
}
