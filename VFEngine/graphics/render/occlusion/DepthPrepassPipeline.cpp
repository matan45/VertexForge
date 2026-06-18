#include "DepthPrepassPipeline.hpp"
#include "DepthPrepass.hpp"
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
        cachedInfo = info;
        createScenePipeline(info);
        createTerrainPipeline(info);
        initialized = true;
        vfLogInfo("Depth prepass pipelines initialized");
    }

    std::vector<vk::Format> DepthPrepassPipeline::colorFormatsForMode() const
    {
        // Normal target is always present. Ray Reconstruction adds diffuse + specular
        // albedo targets (VK-1397). Both prepass pipelines must agree on the count so
        // they stay compatible with the shared prepass render pass.
        std::vector<vk::Format> formats{DepthPrepass::getNormalFormat()};
        if (albedoEnabled)
        {
            formats.push_back(DepthPrepass::getAlbedoFormat());
            formats.push_back(DepthPrepass::getAlbedoFormat());
        }
        return formats;
    }

    void DepthPrepassPipeline::setAlbedoMode(bool enabled)
    {
        if (!initialized || enabled == albedoEnabled)
            return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (scenePipeline) { vkDevice.destroyPipeline(scenePipeline); scenePipeline = nullptr; }
        if (scenePipelineLayout) { vkDevice.destroyPipelineLayout(scenePipelineLayout); scenePipelineLayout = nullptr; }
        if (terrainPipeline) { vkDevice.destroyPipeline(terrainPipeline); terrainPipeline = nullptr; }
        if (terrainPipelineLayout) { vkDevice.destroyPipelineLayout(terrainPipelineLayout); terrainPipelineLayout = nullptr; }
        if (sceneShader) { sceneShader->cleanUp(); sceneShader.reset(); }
        if (terrainShader) { terrainShader->cleanUp(); terrainShader.reset(); }

        albedoEnabled = enabled;
        createScenePipeline(cachedInfo);
        createTerrainPipeline(cachedInfo);
    }

    DepthPrepassPipeline::PipelineCreateResult DepthPrepassPipeline::createDepthOnlyPipeline(
        core::Shader& shader,
        const std::vector<vk::Format>& colorFormats, vk::Format depthFormat,
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

        // One no-blend, write-all attachment state per color target. The prepass writes
        // the normal target (+ optional RR albedo targets, VK-1397); none of them blend.
        vk::PipelineColorBlendAttachmentState noBlend{};
        noBlend.blendEnable = VK_FALSE;
        noBlend.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        std::vector<vk::PipelineColorBlendAttachmentState> blendStates(colorFormats.size(), noBlend);

        core::MeshShaderPipelineConfig config{
            .device = vkDevice,
            .extent = swapChain.getSwapchainExtent(),
            .colorAttachmentFormats = colorFormats,
            .depthAttachmentFormat = depthFormat,
            .shaderStages = stages,
            .existingPipelineLayout = pipelineLayout,
            .cullMode = vk::CullModeFlagBits::eBack,
            .depthTestEnable = true,
            .depthWriteEnable = true,
            .depthCompareOp = vk::CompareOp::eLess,
            .blendEnable = false,
            .colorBlendAttachments = blendStates
        };
        config.dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};

        auto result = core::PipelineUtilities::createMeshShaderPipeline(config);
        return {result.pipeline, pipelineLayout};
    }

    void DepthPrepassPipeline::createScenePipeline(const DepthPrepassInitInfo& info)
    {
        sceneShader = std::make_unique<core::Shader>(device);
        // VK-1397: the albedo permutation emits diffuse/specular albedo guides for DLSS-D
        // Ray Reconstruction (extra MRT outputs in mesh + fragment).
        if (albedoEnabled)
            sceneShader->addMacroDefinition("ALBEDO_PREPASS");
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

        auto result = createDepthOnlyPipeline(*sceneShader, colorFormatsForMode(), info.depthFormat, layouts,
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

        // Terrain reuses the normal-only fragment (no ALBEDO_PREPASS), but must match the
        // shared prepass render pass attachment count, so it declares the same color
        // formats; it simply leaves the albedo targets unwritten (clear value). (VK-1397)
        auto result = createDepthOnlyPipeline(*terrainShader, colorFormatsForMode(), info.depthFormat, layouts,
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
