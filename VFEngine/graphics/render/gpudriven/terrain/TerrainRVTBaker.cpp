#include "TerrainRVTBaker.hpp"
#include "TerrainRVTLayout.hpp" // VK-1620: terrainRVTWorldHeightPlaneIndex
#include "../../../core/Device.hpp"
#include "../../../core/Shader.hpp"
#include "print/Log.hpp"
#include "stats/FrameDrawStats.hpp"

#include <array>

namespace render::gpudriven
{
    TerrainRVTBaker::TerrainRVTBaker(core::Device& device)
        : device(device)
    {
    }

    TerrainRVTBaker::~TerrainRVTBaker()
    {
        cleanup();
    }

    void TerrainRVTBaker::init(vk::DescriptorSetLayout weightMapLayout,
                               vk::DescriptorSetLayout bindlessLayout,
                               vk::DescriptorSetLayout terrainDataLayout,
                               const std::vector<vk::Format>& planeFormats,
                               const TerrainCompositePermutation& permutation)
    {
        if (graphicsPipeline)
            return;

        const vk::Device vkDevice = device.getLogicalDevice();

        core::Shader shader(device);
        // Mirrors TerrainMeshShaderPipeline::loadTerrainShaders — literally the same function on
        // the same struct. Both pipelines #include terrain_material_generated.glsl, so the two
        // macro sets must agree or the baked pages composite differently from the live fallback.
        applyTerrainCompositeMacros(shader, permutation);
        // VK-1620: the world-height plane is NOT part of the composite permutation — it changes
        // what the bake writes, not how the material composites, and the live terrain pipeline does
        // not sample it at all. Derived from the plane list rather than passed separately so the
        // shader's MRT outputs and the pipeline's colour-attachment count cannot disagree: they are
        // both this one vector.
        const bool worldHeight =
            planeFormats.size() > terrainRVTWorldHeightPlaneIndex(permutation.detailMaps);
        if (worldHeight)
            shader.addMacroDefinition("TERRAIN_RVT_WORLD_HEIGHT");
        shader.readShader("../../resources/shaders/gpudriven/terrain_rvt_bake.glsl");
        const auto& stages = shader.getShaderStages();
        if (stages.size() < 2)
        {
            vfLogError("TerrainRVTBaker: failed to load terrain_rvt_bake shader");
            return;
        }

        vk::PushConstantRange pushConstant{};
        pushConstant.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        pushConstant.offset = 0;
        pushConstant.size = sizeof(TilePush);

        std::array<vk::DescriptorSetLayout, 3> layouts = {weightMapLayout, bindlessLayout, terrainDataLayout};

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
        layoutInfo.pSetLayouts = layouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstant;
        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        // No vertex buffer — the quad is generated from gl_VertexIndex.
        vk::PipelineVertexInputStateCreateInfo vertexInput{};

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eNone;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;

        // One blend attachment per MRT plane, blending OFF (the bake overwrites).
        const uint32_t planeCount = static_cast<uint32_t>(planeFormats.size());
        std::vector<vk::PipelineColorBlendAttachmentState> blendAttachments(planeCount);
        for (auto& b : blendAttachments)
        {
            b.blendEnable = VK_FALSE;
            b.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                               vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        }
        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = planeCount;
        colorBlending.pAttachments = blendAttachments.data();

        std::array<vk::DynamicState, 2> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        vk::PipelineDynamicStateCreateInfo dynamicStateInfo{};
        dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicStateInfo.pDynamicStates = dynamicStates.data();

        vk::PipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.colorAttachmentCount = planeCount;
        renderingInfo.pColorAttachmentFormats = planeFormats.data();

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicStateInfo;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.pNext = &renderingInfo;

        auto result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            vfLogError("TerrainRVTBaker: failed to create bake pipeline");
            shader.cleanUp();
            return;
        }
        graphicsPipeline = result.value;
        shader.cleanUp();

        vfLogInfo("TerrainRVTBaker: bake pipeline ready ({} MRT planes)", planeCount);
    }

    void TerrainRVTBaker::cleanup()
    {
        const vk::Device vkDevice = device.getLogicalDevice();
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
    }

    void TerrainRVTBaker::begin(vk::CommandBuffer cmd, const vt::VTPhysicalPool& pool,
                                vk::DescriptorSet weightMapSet, vk::DescriptorSet bindlessSet,
                                vk::DescriptorSet terrainDataSet) const
    {
        if (!graphicsPipeline)
            return;

        std::vector<vk::RenderingAttachmentInfo> colorAttachments(pool.planeCount());
        for (uint32_t i = 0; i < pool.planeCount(); ++i)
        {
            colorAttachments[i].imageView = pool.planeView(i);
            colorAttachments[i].imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            colorAttachments[i].loadOp = vk::AttachmentLoadOp::eLoad;   // preserve other resident pages
            colorAttachments[i].storeOp = vk::AttachmentStoreOp::eStore;
        }

        vk::RenderingInfo renderingInfo{};
        renderingInfo.renderArea = vk::Rect2D{{0, 0}, {pool.getPoolDim(), pool.getPoolDim()}};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
        renderingInfo.pColorAttachments = colorAttachments.data();

        cmd.beginRendering(renderingInfo);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        std::array<vk::DescriptorSet, 3> sets = {weightMapSet, bindlessSet, terrainDataSet};
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0,
                               static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
    }

    void TerrainRVTBaker::beginPage(vk::CommandBuffer cmd, const vt::VTPhysicalPool& pool, uint32_t tile) const
    {
        if (!graphicsPipeline)
            return;

        const vk::Viewport vp = pool.getTileViewport(tile);
        const vk::Rect2D sc = pool.getTileScissor(tile);
        cmd.setViewport(0, 1, &vp);
        cmd.setScissor(0, 1, &sc);

        // Clear this page's tile (Load preserved the atlas; texels not covered by a bake quad
        // — e.g. page area outside the terrain — get a defined value rather than stale content).
        std::vector<vk::ClearAttachment> clears(pool.planeCount());
        for (uint32_t i = 0; i < pool.planeCount(); ++i)
        {
            clears[i].aspectMask = vk::ImageAspectFlagBits::eColor;
            clears[i].colorAttachment = i;
            clears[i].clearValue.color = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
        }
        vk::ClearRect clearRect{};
        clearRect.rect = sc;
        clearRect.baseArrayLayer = 0;
        clearRect.layerCount = 1;
        cmd.clearAttachments(static_cast<uint32_t>(clears.size()), clears.data(), 1, &clearRect);
    }

    void TerrainRVTBaker::drawTile(vk::CommandBuffer cmd, const TilePush& push) const
    {
        if (!graphicsPipeline)
            return;
        cmd.pushConstants(pipelineLayout,
                          vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                          0, sizeof(TilePush), &push);
        cmd.draw(6, 1, 0, 0);
        render::FrameDrawStats::count(render::DrawCategory::VirtualTexture);
    }

    void TerrainRVTBaker::end(vk::CommandBuffer cmd) const
    {
        if (!graphicsPipeline)
            return;
        cmd.endRendering();
    }
}
