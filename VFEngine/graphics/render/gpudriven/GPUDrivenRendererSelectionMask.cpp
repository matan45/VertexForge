#include "GPUDrivenRenderer.hpp"
#include "SelectionMaskPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "print/Log.hpp"

// VK-1490 editor selection outline, mask recording. Mirrors the depth-prepass
// scene block (GPUDrivenRendererDepthPrepass.cpp): the SAME post-cull combined
// indirect stream + live descriptor sets, but through SelectionMaskPipeline,
// whose task shader early-outs every draw whose object slot is not in the
// selection bitmask. The fragment shader samples resolved scene depth with a
// conservative neighborhood test for a stable visible-only silhouette.

namespace render::gpudriven
{
    bool GPUDrivenRenderer::ensureSelectionMaskResources()
    {
        if (!initialized || !enabled || !meshShaderPipeline || !batchManager ||
            !bindlessTextures || !boneMatrixManager || !mergedBuffer)
        {
            return false;
        }

        if (!selectionMaskPipeline)
        {
            selectionMaskPipeline = std::make_unique<SelectionMaskPipeline>(device, swapChain);
            SelectionMaskInitInfo info{
                .cameraLayout = cachedIBLLayout,
                .perDrawLayout = meshShaderPipeline->getPerDrawDataLayout(),
                .bindlessTextureLayout = bindlessTextures->getDescriptorSetLayout(),
                .meshletDataLayout = meshShaderPipeline->getMeshletDataLayout(),
                .vertexDataLayout = meshShaderPipeline->getVertexDataLayout(),
                .boneMatrixLayout = boneMatrixManager->getDescriptorSetLayout(),
                .maxObjectCount = mergedBuffer->getMaxObjectCount()
            };
            selectionMaskPipeline->init(info);
        }

        if (!selectionMaskPipeline->isInitialized())
        {
            return false;
        }

        selectionMaskPipeline->ensureMaskTarget(swapChain.getSwapchainExtent());
        return selectionMaskPipeline->getMaskImageView() != nullptr;
    }

    void GPUDrivenRenderer::renderSelectionMask(vk::CommandBuffer cmd,
                                                vk::DescriptorSet iblDescriptorSet,
                                                vk::ImageView sceneDepthView)
    {
        if (!selectionMaskPipeline || !selectionMaskPipeline->isInitialized())
        {
            return;
        }

        // Rebinds only when the engine-owned depth view changes on recreation.
        selectionMaskPipeline->updateSceneDepthInput(sceneDepthView);

        const auto& selectedSlots = mergedBuffer->getSelectedObjectSlots();
        const auto extent = selectionMaskPipeline->getMaskExtent();

        // The bits were written by updateScene right after the slot rebuild
        // (same-frame snapshot); here we only bind that ring entry.
        auto bitsSet = selectionMaskPipeline->getCurrentBitsDescriptorSet();

        // Always begin/end: the frame graph transitioned the mask for a write,
        // and an empty selection must still leave a cleared mask behind.
        selectionMaskPipeline->beginMaskPass(cmd);

        if (!selectedSlots.empty() && stats.totalObjects > 0)
        {
            selectionMaskPipeline->bindPipeline(cmd);
            auto layout = selectionMaskPipeline->getPipelineLayout();

            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0,
                                   iblDescriptorSet, {});
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 1,
                                   meshShaderPipeline->getPerDrawDataDescriptorSet(), {});
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 2,
                                   bindlessTextures->getDescriptorSet(), {});

            std::array<vk::DescriptorSet, 2> meshletVertexSets = {
                meshShaderPipeline->getMeshletDataDescriptorSet(),
                meshShaderPipeline->getVertexDataDescriptorSet()
            };
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 3,
                                   meshletVertexSets, {});

            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 5,
                                   boneMatrixManager->getDescriptorSet(), {});
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 6,
                                   bitsSet, {});

            uint32_t batchCount = batchManager->getBatchCount();
            uint32_t commandsPerSection = batchManager->getCommandsPerSection();

            for (uint32_t shaderGroup = 0; shaderGroup <= 2; ++shaderGroup)
            {
                for (uint32_t batch = 0; batch < batchCount; ++batch)
                {
                    if (!batchManager->sectionHasCandidates(batch, shaderGroup)) continue;

                    vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(batch, shaderGroup);
                    vk::DeviceSize countOffset = batchManager->getDrawCountOffset(batch, shaderGroup);

                    SelectionMaskPushConstants pc{};
                    pc.baseDrawIndex = batchManager->getSectionIndex(batch, shaderGroup) * commandsPerSection;
                    pc.viewMode = MESHLET_CULL_FRUSTUM_BIT;
                    pc.screenWidth = static_cast<float>(extent.width);
                    pc.screenHeight = static_cast<float>(extent.height);
                    selectionMaskPipeline->pushConstants(cmd, pc);

                    render::FrameDrawStats::count(render::DrawCategory::Meshes);
                    cmd.drawMeshTasksIndirectCountEXT(
                        batchManager->getCombinedDrawCommandBuffer(),
                        cmdOffset,
                        batchManager->getCombinedDrawCountBuffer(),
                        countOffset,
                        commandsPerSection,
                        sizeof(MeshTasksIndirectCommand));
                }
            }
        }

        selectionMaskPipeline->endMaskPass(cmd);
    }
}
