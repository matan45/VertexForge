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

    void DepthPrepassPipeline::init(vk::RenderPass depthRenderPass,
                                     vk::DescriptorSetLayout cameraLayout,
                                     vk::DescriptorSetLayout perDrawLayout,
                                     vk::DescriptorSetLayout bindlessTextureLayout,
                                     vk::DescriptorSetLayout meshletDataLayout,
                                     vk::DescriptorSetLayout vertexDataLayout,
                                     vk::DescriptorSetLayout boneMatrixLayout,
                                     vk::DescriptorSetLayout terrainDataLayout)
    {
        createScenePipeline(depthRenderPass, cameraLayout, perDrawLayout,
                            bindlessTextureLayout, meshletDataLayout,
                            vertexDataLayout, boneMatrixLayout);
        createTerrainPipeline(depthRenderPass, cameraLayout, meshletDataLayout,
                              vertexDataLayout, terrainDataLayout);

        initialized = true;
        vfLogInfo("Depth prepass pipelines initialized");
    }

    void DepthPrepassPipeline::createScenePipeline(
        vk::RenderPass renderPass,
        vk::DescriptorSetLayout cameraLayout,
        vk::DescriptorSetLayout perDrawLayout,
        vk::DescriptorSetLayout bindlessTextureLayout,
        vk::DescriptorSetLayout meshletDataLayout,
        vk::DescriptorSetLayout vertexDataLayout,
        vk::DescriptorSetLayout boneMatrixLayout)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        sceneShader = std::make_unique<core::Shader>(device);
        sceneShader->readShader("../../resources/shaders/depthprepass/task_depth_prepass.glsl");
        sceneShader->readShader("../../resources/shaders/depthprepass/mesh_depth_prepass.glsl");

        const auto& stages = sceneShader->getShaderStages();
        if (stages.size() < 2)
        {
            vfLogError("DepthPrepassPipeline: Failed to load scene depth prepass shaders: {}",
                        sceneShader->getLastCompilationError());
            return;
        }

        // Set layout matches the scene pipeline: set 0=camera, set 1=perDraw, set 2=bindless,
        // set 3=meshlet, set 4=vertex, set 5=bones
        std::vector<vk::DescriptorSetLayout> setLayouts = {
            cameraLayout,
            perDrawLayout,
            bindlessTextureLayout,
            meshletDataLayout,
            vertexDataLayout,
            boneMatrixLayout
        };

        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eTaskEXT |
                                       vk::ShaderStageFlagBits::eMeshEXT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(DepthPrepassPushConstants);

        vk::PipelineLayoutCreateInfo layoutCreateInfo{};
        layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutCreateInfo.pSetLayouts = setLayouts.data();
        layoutCreateInfo.pushConstantRangeCount = 1;
        layoutCreateInfo.pPushConstantRanges = &pushConstantRange;

        scenePipelineLayout = vkDevice.createPipelineLayout(layoutCreateInfo);

        core::MeshShaderPipelineConfig config{
            .device = vkDevice,
            .renderPass = renderPass,
            .extent = swapChain.getSwapchainExtent(),
            .shaderStages = stages,
            .existingPipelineLayout = scenePipelineLayout,
            .cullMode = vk::CullModeFlagBits::eBack,
            .depthTestEnable = true,
            .depthWriteEnable = true,
            .depthCompareOp = vk::CompareOp::eLess,
            .blendEnable = false
        };
        config.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };

        auto result = core::PipelineUtilities::createMeshShaderPipeline(config);
        scenePipeline = result.pipeline;
    }

    void DepthPrepassPipeline::createTerrainPipeline(
        vk::RenderPass renderPass,
        vk::DescriptorSetLayout cameraLayout,
        vk::DescriptorSetLayout meshletDataLayout,
        vk::DescriptorSetLayout vertexDataLayout,
        vk::DescriptorSetLayout terrainDataLayout)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        terrainShader = std::make_unique<core::Shader>(device);
        terrainShader->readShader("../../resources/shaders/depthprepass/task_terrain_depth_prepass.glsl");
        terrainShader->readShader("../../resources/shaders/depthprepass/mesh_terrain_depth_prepass.glsl");

        const auto& stages = terrainShader->getShaderStages();
        if (stages.size() < 2)
        {
            vfLogError("DepthPrepassPipeline: Failed to load terrain depth prepass shaders: {}",
                        terrainShader->getLastCompilationError());
            return;
        }

        // Terrain layout: set 0=camera, set 1=empty, set 2=empty, set 3=meshlet,
        // set 4=vertex, ... set 11=terrain
        // We need to match the full terrain pipeline layout for descriptor set binding compatibility.
        // Use empty layouts for unused sets, or pad with the meshlet/vertex layouts.
        // Simpler approach: create a dedicated layout with only the sets we use.

        // Build layout array with 12 slots (0..11)
        // Create empty layout for unused sets
        vk::DescriptorSetLayoutCreateInfo emptyLayoutInfo{};
        emptyLayoutInfo.bindingCount = 0;
        vk::DescriptorSetLayout emptyLayout = vkDevice.createDescriptorSetLayout(emptyLayoutInfo);

        std::vector<vk::DescriptorSetLayout> setLayouts(12, emptyLayout);
        setLayouts[0] = cameraLayout;        // Camera UBO
        setLayouts[3] = meshletDataLayout;   // Meshlets
        setLayouts[4] = vertexDataLayout;    // Vertices
        setLayouts[11] = terrainDataLayout;  // Terrain tile data

        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eTaskEXT |
                                       vk::ShaderStageFlagBits::eMeshEXT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(TerrainDepthPrepassPushConstants);

        vk::PipelineLayoutCreateInfo layoutCreateInfo{};
        layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutCreateInfo.pSetLayouts = setLayouts.data();
        layoutCreateInfo.pushConstantRangeCount = 1;
        layoutCreateInfo.pPushConstantRanges = &pushConstantRange;

        terrainPipelineLayout = vkDevice.createPipelineLayout(layoutCreateInfo);

        // Clean up the temporary empty layout
        vkDevice.destroyDescriptorSetLayout(emptyLayout);

        core::MeshShaderPipelineConfig config{
            .device = vkDevice,
            .renderPass = renderPass,
            .extent = swapChain.getSwapchainExtent(),
            .shaderStages = stages,
            .existingPipelineLayout = terrainPipelineLayout,
            .cullMode = vk::CullModeFlagBits::eBack,
            .depthTestEnable = true,
            .depthWriteEnable = true,
            .depthCompareOp = vk::CompareOp::eLess,
            .blendEnable = false
        };
        config.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };

        auto result = core::PipelineUtilities::createMeshShaderPipeline(config);
        terrainPipeline = result.pipeline;
    }

    void DepthPrepassPipeline::bindScenePipeline(vk::CommandBuffer cmd) const
    {
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, scenePipeline);
    }

    void DepthPrepassPipeline::pushSceneConstants(vk::CommandBuffer cmd, const DepthPrepassPushConstants& pc) const
    {
        cmd.pushConstants(scenePipelineLayout,
                          vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT,
                          0, sizeof(DepthPrepassPushConstants), &pc);
    }

    void DepthPrepassPipeline::bindTerrainPipeline(vk::CommandBuffer cmd) const
    {
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, terrainPipeline);
    }

    void DepthPrepassPipeline::pushTerrainConstants(vk::CommandBuffer cmd, const TerrainDepthPrepassPushConstants& pc) const
    {
        cmd.pushConstants(terrainPipelineLayout,
                          vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT,
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
