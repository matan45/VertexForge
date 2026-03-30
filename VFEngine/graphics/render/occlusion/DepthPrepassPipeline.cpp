#include "DepthPrepassPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"

namespace render::occlusion
{
    DepthPrepassPipeline::DepthPrepassPipeline(core::Device& device, core::SwapChain& swapChain)
        : device(device), swapChain(swapChain)
    {
    }

    DepthPrepassPipeline::~DepthPrepassPipeline()
    {
        cleanup();
    }

    void DepthPrepassPipeline::init(const DepthPrepassInitInfo& info)
    {
        createScenePipeline(info);
        createTerrainPipeline(info);
        initialized = true;
        vfLogInfo("Depth prepass pipelines initialized");
    }

    DepthPrepassPipeline::PipelineCreateResult DepthPrepassPipeline::createDepthOnlyPipeline(
        core::Shader& shader, vk::RenderPass renderPass,
        const std::vector<vk::DescriptorSetLayout>& layouts,
        uint32_t pushConstantSize)
    {
        vk::Device vkDevice = device.getLogicalDevice();
        const auto& stages = shader.getShaderStages();

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT |
                               vk::ShaderStageFlagBits::eFragment;
        pushRange.offset = 0;
        pushRange.size = pushConstantSize;

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
        layoutInfo.pSetLayouts = layouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        vk::PipelineLayout pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        // Color blend attachment for normal output (no blending, write all channels)
        vk::PipelineColorBlendAttachmentState normalBlend{};
        normalBlend.blendEnable = VK_FALSE;
        normalBlend.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                     vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

        core::MeshShaderPipelineConfig config{
            .device = vkDevice,
            .renderPass = renderPass,
            .extent = swapChain.getSwapchainExtent(),
            .shaderStages = stages,
            .existingPipelineLayout = pipelineLayout,
            .cullMode = vk::CullModeFlagBits::eBack,
            .depthTestEnable = true,
            .depthWriteEnable = true,
            .depthCompareOp = vk::CompareOp::eLess,
            .blendEnable = false,
            .colorBlendAttachments = {normalBlend}
        };
        config.dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};

        auto result = core::PipelineUtilities::createMeshShaderPipeline(config);
        return {result.pipeline, pipelineLayout};
    }

    void DepthPrepassPipeline::createScenePipeline(const DepthPrepassInitInfo& info)
    {
        sceneShader = std::make_unique<core::Shader>(device);
        sceneShader->readShader("../../resources/shaders/depthprepass/task_depth_prepass.glsl");
        sceneShader->readShader("../../resources/shaders/depthprepass/mesh_depth_prepass.glsl");
        sceneShader->readShader("../../resources/shaders/depthprepass/frag_depth_prepass.glsl");

        if (sceneShader->getShaderStages().size() < 3)
        {
            vfLogError("DepthPrepassPipeline: Failed to load scene shaders: {}",
                        sceneShader->getLastCompilationError());
            return;
        }

        std::vector<vk::DescriptorSetLayout> layouts = {
            info.cameraLayout, info.perDrawLayout, info.bindlessTextureLayout,
            info.meshletDataLayout, info.vertexDataLayout, info.boneMatrixLayout
        };

        auto result = createDepthOnlyPipeline(*sceneShader, info.renderPass, layouts,
                                               sizeof(DepthPrepassPushConstants));
        scenePipeline = result.pipeline;
        scenePipelineLayout = result.layout;
    }

    void DepthPrepassPipeline::createTerrainPipeline(const DepthPrepassInitInfo& info)
    {
        terrainShader = std::make_unique<core::Shader>(device);
        terrainShader->readShader("../../resources/shaders/depthprepass/task_terrain_depth_prepass.glsl");
        terrainShader->readShader("../../resources/shaders/depthprepass/mesh_terrain_depth_prepass.glsl");
        terrainShader->readShader("../../resources/shaders/depthprepass/frag_depth_prepass.glsl");

        if (terrainShader->getShaderStages().size() < 3)
        {
            vfLogError("DepthPrepassPipeline: Failed to load terrain shaders: {}",
                        terrainShader->getLastCompilationError());
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();
        vk::DescriptorSetLayoutCreateInfo emptyLayoutInfo{};
        vk::DescriptorSetLayout emptyLayout = vkDevice.createDescriptorSetLayout(emptyLayoutInfo);

        std::vector<vk::DescriptorSetLayout> layouts(12, emptyLayout);
        layouts[0] = info.cameraLayout;
        layouts[3] = info.meshletDataLayout;
        layouts[4] = info.vertexDataLayout;
        layouts[11] = info.terrainDataLayout;

        auto result = createDepthOnlyPipeline(*terrainShader, info.renderPass, layouts,
                                               sizeof(TerrainDepthPrepassPushConstants));
        terrainPipeline = result.pipeline;
        terrainPipelineLayout = result.layout;

        vkDevice.destroyDescriptorSetLayout(emptyLayout);
    }

    void DepthPrepassPipeline::bindScenePipeline(vk::CommandBuffer cmd) const
    {
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, scenePipeline);
    }

    void DepthPrepassPipeline::pushSceneConstants(vk::CommandBuffer cmd, const DepthPrepassPushConstants& pc) const
    {
        cmd.pushConstants(scenePipelineLayout,
                          vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT | vk::ShaderStageFlagBits::eFragment,
                          0, sizeof(DepthPrepassPushConstants), &pc);
    }

    void DepthPrepassPipeline::bindTerrainPipeline(vk::CommandBuffer cmd) const
    {
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, terrainPipeline);
    }

    void DepthPrepassPipeline::pushTerrainConstants(vk::CommandBuffer cmd, const TerrainDepthPrepassPushConstants& pc) const
    {
        cmd.pushConstants(terrainPipelineLayout,
                          vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT | vk::ShaderStageFlagBits::eFragment,
                          0, sizeof(TerrainDepthPrepassPushConstants), &pc);
    }

    void DepthPrepassPipeline::cleanup()
    {
        if (!initialized) return;
        device.getLogicalDevice().waitIdle();

        if (scenePipeline) device.getLogicalDevice().destroyPipeline(scenePipeline);
        if (scenePipelineLayout) device.getLogicalDevice().destroyPipelineLayout(scenePipelineLayout);
        if (terrainPipeline) device.getLogicalDevice().destroyPipeline(terrainPipeline);
        if (terrainPipelineLayout) device.getLogicalDevice().destroyPipelineLayout(terrainPipelineLayout);

        if (sceneShader) { sceneShader->cleanUp(); sceneShader.reset(); }
        if (terrainShader) { terrainShader->cleanUp(); terrainShader.reset(); }

        initialized = false;
    }
}
