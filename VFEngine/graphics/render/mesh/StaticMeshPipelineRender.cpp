#include "StaticMeshPipeline.hpp"
#include "MeshGPUCache.hpp"
#include "MaterialCacheManager.hpp"
#include "../DebugRenderer.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "../material/MaterialShaderCache.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "../../core/ImageUtilities.hpp"
#include "material/MaterialTypes.hpp"
#include "math/Frustum.hpp"
#include <algorithm>

namespace render::mesh
{
    void StaticMeshPipeline::collectSortedSubmeshes(
        const std::vector<MeshRenderData>& meshDrawList,
        const math::Frustum* frustum,
        const std::unordered_map<std::string, std::shared_ptr<material::MaterialData>>& materialCache,
        std::vector<SortedSubmesh>& opaqueSubmeshes,
        std::vector<SortedSubmesh>& maskedSubmeshes) const
    {
        opaqueSubmeshes.reserve(256);
        maskedSubmeshes.reserve(64);

        for (const auto& meshData : meshDrawList)
        {
            const MeshGPUData* gpuData = getMesh(meshData.meshPath);
            if (!gpuData || gpuData->subMeshes.empty()) continue;

            for (size_t subMeshIndex = 0; subMeshIndex < gpuData->subMeshes.size(); ++subMeshIndex)
            {
                const auto& subMesh = gpuData->subMeshes[subMeshIndex];

                if (frustum && frustum->isInitialized() &&
                    !frustum->intersectsAABB(subMesh.boundingBox, meshData.modelMatrix))
                    continue;

                ExtractedPBRValues pbrValues = MaterialPBRExtractor::getPBRForSubmesh(
                    meshData, subMesh.name, materialCache, currentTime);

                if (pbrValues.blendMode == material::BlendMode::Opaque ||
                    pbrValues.blendMode == material::BlendMode::Translucent ||
                    pbrValues.blendMode == material::BlendMode::Additive ||
                    pbrValues.blendMode == material::BlendMode::Multiply)
                {
                    opaqueSubmeshes.push_back({&meshData, &subMesh, subMeshIndex, pbrValues.materialPath});
                }
                else if (pbrValues.blendMode == material::BlendMode::Masked)
                {
                    maskedSubmeshes.push_back({&meshData, &subMesh, subMeshIndex, pbrValues.materialPath});
                }
            }
        }

        auto materialSortComparator = [](const SortedSubmesh& a, const SortedSubmesh& b) {
            return a.materialPath < b.materialPath;
        };
        std::sort(opaqueSubmeshes.begin(), opaqueSubmeshes.end(), materialSortComparator);
        std::sort(maskedSubmeshes.begin(), maskedSubmeshes.end(), materialSortComparator);
    }

    void StaticMeshPipeline::bindSubmeshPipeline(
        const vk::CommandBuffer& commandBuffer,
        const ExtractedPBRValues& pbrValues,
        const std::unordered_map<std::string, std::shared_ptr<material::MaterialData>>& materialCache,
        RenderState& state) const
    {
        vk::Pipeline targetPipeline = graphicsPipeline;

        if (materialShaderCache && !pbrValues.materialPath.empty())
        {
            auto matIt = materialCache.find(pbrValues.materialPath);
            if (matIt != materialCache.end() && matIt->second)
            {
                const material::MaterialData& matData = *matIt->second;
                if (!matData.cachedVertexShader.empty() && !matData.cachedFragmentShader.empty())
                {
                    const MaterialPipelineData* matPipeline =
                        materialShaderCache->getOrCreatePipeline(pbrValues.materialPath, matData);
                    if (matPipeline && matPipeline->valid)
                    {
                        targetPipeline = (pbrValues.blendMode == material::BlendMode::Masked)
                            ? matPipeline->maskedPipeline
                            : matPipeline->opaquePipeline;
                    }
                }
            }
        }

        if (state.currentPipeline != targetPipeline)
        {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, targetPipeline);
            state.currentPipeline = targetPipeline;
        }
    }

    void StaticMeshPipeline::bindSubmeshMaterial(
        const vk::CommandBuffer& commandBuffer,
        const ExtractedPBRValues& pbrValues,
        RenderState& state) const
    {
        vk::DescriptorSet materialDescSet = nullptr;
        bool hasAnyTexture = !pbrValues.albedoTexturePath.empty() ||
            !pbrValues.normalTexturePath.empty() ||
            !pbrValues.ormTexturePath.empty() ||
            !pbrValues.metallicTexturePath.empty() ||
            !pbrValues.roughnessTexturePath.empty() ||
            !pbrValues.aoTexturePath.empty() ||
            !pbrValues.emissionTexturePath.empty();

        if (hasAnyTexture && !pbrValues.materialPath.empty())
        {
            MaterialTexturePaths texPaths;
            texPaths.albedo = pbrValues.albedoTexturePath;
            texPaths.normal = pbrValues.normalTexturePath;
            texPaths.orm = pbrValues.ormTexturePath;
            texPaths.metallic = pbrValues.metallicTexturePath;
            texPaths.roughness = pbrValues.roughnessTexturePath;
            texPaths.ao = pbrValues.aoTexturePath;
            texPaths.emission = pbrValues.emissionTexturePath;

            materialDescSet = textureCache->getOrCreateMaterialDescriptorSet(
                pbrValues.materialPath, texPaths);
        }

        if (materialDescSet && materialDescSet != state.currentMaterialDescriptorSet)
        {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                             pipelineLayout, 1, materialDescSet, nullptr);
            state.currentMaterialDescriptorSet = materialDescSet;
        }
        else if (!materialDescSet && state.currentMaterialDescriptorSet != state.defaultMaterialDescriptorSet)
        {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                             pipelineLayout, 1, state.defaultMaterialDescriptorSet, nullptr);
            state.currentMaterialDescriptorSet = state.defaultMaterialDescriptorSet;
        }
    }

    MeshPushConstants StaticMeshPipeline::buildSubmeshPushConstants(
        const MeshRenderData& meshData,
        const SubMeshGPUData& subMesh,
        size_t subMeshIndex,
        const ExtractedPBRValues& pbrValues) const
    {
        MeshPushConstants pushConstants{};
        pushConstants.model = meshData.modelMatrix;
        pushConstants.metallic = pbrValues.metallic;
        pushConstants.roughness = pbrValues.roughness;
        pushConstants.ao = pbrValues.ao;
        pushConstants.blendMode = static_cast<float>(pbrValues.blendMode);
        pushConstants.alphaCutoff = pbrValues.alphaCutoff;

        auto getTexIdx = [&](material::TextureSlot slot, const std::string& pbrPath) -> uint8_t {
            if (!pbrPath.empty()) {
                return static_cast<uint8_t>(material::toIndex(slot));
            }
            float explicitIdx = meshData.textureIndices[material::toIndex(slot)];
            if (explicitIdx >= 0.0f) {
                return static_cast<uint8_t>(explicitIdx);
            }
            return TEXTURE_INDEX_NONE;
        };

        pushConstants.textureIndicesPacked[0] = packTextureIndices(
            getTexIdx(material::TextureSlot::Albedo, pbrValues.albedoTexturePath),
            getTexIdx(material::TextureSlot::Normal, pbrValues.normalTexturePath),
            getTexIdx(material::TextureSlot::ORM, pbrValues.ormTexturePath),
            getTexIdx(material::TextureSlot::Metallic, pbrValues.metallicTexturePath)
        );
        pushConstants.textureIndicesPacked[1] = packTextureIndices(
            getTexIdx(material::TextureSlot::Roughness, pbrValues.roughnessTexturePath),
            getTexIdx(material::TextureSlot::AO, pbrValues.aoTexturePath),
            getTexIdx(material::TextureSlot::Emission, pbrValues.emissionTexturePath),
            getTexIdx(material::TextureSlot::Height, pbrValues.heightTexturePath)
        );
        pushConstants.textureIndicesPacked[2] = packTextureIndices(
            TEXTURE_INDEX_NONE, TEXTURE_INDEX_NONE, TEXTURE_INDEX_NONE, TEXTURE_INDEX_NONE);
        pushConstants.textureIndicesPacked[3] = packTextureIndices(
            TEXTURE_INDEX_NONE, TEXTURE_INDEX_NONE, TEXTURE_INDEX_NONE, TEXTURE_INDEX_NONE);

        pushConstants.iblDiffuse = pbrValues.iblDiffuse;
        pushConstants.iblSpecular = pbrValues.iblSpecular;

        if (meshData.highlightedSubMesh >= 0 &&
            static_cast<size_t>(meshData.highlightedSubMesh) == subMeshIndex)
        {
            pushConstants.albedo = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
            pushConstants.emission = 0.8f;
        }
        else
        {
            pushConstants.albedo = pbrValues.albedo;
            pushConstants.emission = pbrValues.emission;
        }

        return pushConstants;
    }

    void StaticMeshPipeline::renderSubmesh(
        const vk::CommandBuffer& commandBuffer,
        const MeshRenderData& meshData,
        const SubMeshGPUData& subMesh,
        size_t subMeshIndex,
        material::BlendMode targetBlendMode,
        const std::unordered_map<std::string, std::shared_ptr<material::MaterialData>>& materialCache,
        RenderState& state) const
    {
        ExtractedPBRValues pbrValues = MaterialPBRExtractor::getPBRForSubmesh(
            meshData, subMesh.name, materialCache, currentTime);

        if (pbrValues.blendMode != targetBlendMode)
        {
            // In traditional pipeline, transparent modes render using the opaque pipeline
            if (targetBlendMode != material::BlendMode::Opaque ||
                !material::isTransparentBlendMode(pbrValues.blendMode))
                return;
        }

        bindSubmeshPipeline(commandBuffer, pbrValues, materialCache, state);
        bindSubmeshMaterial(commandBuffer, pbrValues, state);

        MeshPushConstants pushConstants = buildSubmeshPushConstants(meshData, subMesh, subMeshIndex, pbrValues);
        commandBuffer.pushConstants(pipelineLayout,
                                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                    0, sizeof(MeshPushConstants), &pushConstants);

        uint32_t lodLevel = (meshData.forceLODLevel >= 0
            && meshData.forceLODLevel < static_cast<int>(resource::LOD_LEVEL_COUNT))
            ? static_cast<uint32_t>(meshData.forceLODLevel) : 0;
        const auto& lodBuffers = subMesh.getLOD(lodLevel);

        if (!lodBuffers.isValid()) return;

        vk::Buffer vertexBuffers[] = {lodBuffers.vertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

        if (lodBuffers.indexCount > 0)
        {
            commandBuffer.bindIndexBuffer(lodBuffers.indexBuffer, 0, vk::IndexType::eUint32);
            commandBuffer.drawIndexed(lodBuffers.indexCount, 1, 0, 0, 0);
            render::FrameDrawStats::count();
        }
        else
        {
            commandBuffer.draw(lodBuffers.vertexCount, 1, 0, 0);
            render::FrameDrawStats::count();
        }
    }

    void StaticMeshPipeline::recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                                 uint32_t imageIndex,
                                                 const std::vector<MeshRenderData>& meshDrawList,
                                                 const math::Frustum* frustum,
                                                 render::DebugRenderer* debugRenderer,
                                                 const glm::mat4& debugView,
                                                 const glm::mat4& debugProjection) const
    {
        bool hasDebugItems = debugRenderer && debugRenderer->hasItemsToRender();
        if (meshDrawList.empty() && !hasDebugItems)
        {
            return;
        }

        if (!meshDrawList.empty())
        {
            prepareTexturesForFrame(meshDrawList);
        }

        auto cacheLock = materialCacheManager->acquireSharedLock();
        const auto& materialCache = materialCacheManager->getCache();

        beginRenderPass(commandBuffer, imageIndex);

        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                         pipelineLayout, 0, descriptorSet, nullptr);

        vk::DescriptorSet frameTextureDescriptorSet = getTextureDescriptorSet(imageIndex);
        if (textureDescriptorsInitialized && frameTextureDescriptorSet)
        {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                             pipelineLayout, 1, frameTextureDescriptorSet, nullptr);
        }

        std::vector<SortedSubmesh> opaqueSubmeshes;
        std::vector<SortedSubmesh> maskedSubmeshes;
        collectSortedSubmeshes(meshDrawList, frustum, materialCache, opaqueSubmeshes, maskedSubmeshes);

        RenderState state;
        state.currentMaterialDescriptorSet = frameTextureDescriptorSet;
        state.defaultMaterialDescriptorSet = frameTextureDescriptorSet;

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        state.currentPipeline = graphicsPipeline;

        for (const auto& item : opaqueSubmeshes)
        {
            renderSubmesh(commandBuffer, *item.meshData, *item.subMesh, item.subMeshIndex,
                         material::BlendMode::Opaque, materialCache, state);
        }

        for (const auto& item : maskedSubmeshes)
        {
            renderSubmesh(commandBuffer, *item.meshData, *item.subMesh, item.subMeshIndex,
                         material::BlendMode::Masked, materialCache, state);
        }

        if (debugRenderer && debugRenderer->hasItemsToRender())
        {
            debugRenderer->render(commandBuffer, meshDrawList, debugView, debugProjection,
                                  [this](const std::string& meshId) { return getMesh(meshId); });
        }

        endRenderPass(commandBuffer, imageIndex);
    }

    void StaticMeshPipeline::renderMeshList(const vk::CommandBuffer& commandBuffer,
                                            uint32_t imageIndex,
                                            const std::vector<MeshRenderData>& meshDrawList,
                                            const math::Frustum* frustum) const
    {
        if (meshDrawList.empty())
        {
            return;
        }

        prepareTexturesForFrame(meshDrawList);

        auto cacheLock = materialCacheManager->acquireSharedLock();
        const auto& materialCache = materialCacheManager->getCache();

        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                         pipelineLayout, 0, descriptorSet, nullptr);

        vk::DescriptorSet frameTextureDescriptorSet = getTextureDescriptorSet(imageIndex);
        if (textureDescriptorsInitialized && frameTextureDescriptorSet)
        {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                             pipelineLayout, 1, frameTextureDescriptorSet, nullptr);
        }

        std::vector<SortedSubmesh> opaqueSubmeshes;
        std::vector<SortedSubmesh> maskedSubmeshes;
        collectSortedSubmeshes(meshDrawList, frustum, materialCache, opaqueSubmeshes, maskedSubmeshes);

        RenderState state;
        state.currentMaterialDescriptorSet = frameTextureDescriptorSet;
        state.defaultMaterialDescriptorSet = frameTextureDescriptorSet;

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        state.currentPipeline = graphicsPipeline;

        for (const auto& item : opaqueSubmeshes)
        {
            renderSubmesh(commandBuffer, *item.meshData, *item.subMesh, item.subMeshIndex,
                         material::BlendMode::Opaque, materialCache, state);
        }

        for (const auto& item : maskedSubmeshes)
        {
            renderSubmesh(commandBuffer, *item.meshData, *item.subMesh, item.subMeshIndex,
                         material::BlendMode::Masked, materialCache, state);
        }
    }

    void StaticMeshPipeline::prepareTexturesForFrame(const std::vector<MeshRenderData>& meshDrawList) const
    {
        bool hasMaterials = false;
        for (const auto& meshData : meshDrawList)
        {
            if (!meshData.defaultMaterialPath.empty() || !meshData.submeshMaterials.empty())
            {
                hasMaterials = true;
                break;
            }
        }

        if (!hasMaterials)
        {
            return;
        }

        materialCacheManager->checkAndClearInvalidation();

        auto loadTexturesFromMaterial = [this](const std::shared_ptr<material::MaterialData>& matData)
        {
            if (!matData) return;

            ExtractedPBRValues pbr = MaterialPBRExtractor::extractPBRFromMaterial(*matData);

            if (!pbr.albedoTexturePath.empty())
                textureCache->loadTexture(pbr.albedoTexturePath, vk::Format::eR8G8B8A8Srgb);
            if (!pbr.normalTexturePath.empty())
                textureCache->loadTexture(pbr.normalTexturePath);
            if (!pbr.ormTexturePath.empty())
                textureCache->loadTexture(pbr.ormTexturePath);
            if (!pbr.metallicTexturePath.empty())
                textureCache->loadTexture(pbr.metallicTexturePath);
            if (!pbr.roughnessTexturePath.empty())
                textureCache->loadTexture(pbr.roughnessTexturePath);
            if (!pbr.aoTexturePath.empty())
                textureCache->loadTexture(pbr.aoTexturePath);
            if (!pbr.emissionTexturePath.empty())
                textureCache->loadTexture(pbr.emissionTexturePath, vk::Format::eR8G8B8A8Srgb);
            if (!pbr.heightTexturePath.empty())
                textureCache->loadTexture(pbr.heightTexturePath);
        };

        for (const auto& meshData : meshDrawList)
        {
            if (!meshData.defaultMaterialPath.empty())
            {
                auto matData = materialCacheManager->getMaterial(meshData.defaultMaterialPath);
                loadTexturesFromMaterial(matData);
            }

            for (const auto& [submeshName, matInfo] : meshData.submeshMaterials)
            {
                if (!matInfo.materialPath.empty())
                {
                    auto matData = materialCacheManager->getMaterial(matInfo.materialPath);
                    loadTexturesFromMaterial(matData);
                }
            }
        }
    }

    void StaticMeshPipeline::updatePreviewTextureDescriptors(
        uint32_t imageIndex,
        const std::array<vk::ImageView, material::MAX_MATERIAL_TEXTURES>& imageViews,
        const std::array<vk::Sampler, material::MAX_MATERIAL_TEXTURES>& samplers)
    {
        vk::DescriptorSet frameTextureDescriptorSet = getTextureDescriptorSet(imageIndex);
        if (!frameTextureDescriptorSet) return;

        std::array<vk::DescriptorImageInfo, material::MAX_MATERIAL_TEXTURES> imageInfos;
        for (int i = 0; i < material::MAX_MATERIAL_TEXTURES; ++i)
        {
            imageInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            imageInfos[i].imageView = imageViews[i];
            imageInfos[i].sampler = samplers[i];
        }

        vk::WriteDescriptorSet writeSet{};
        writeSet.dstSet = frameTextureDescriptorSet;
        writeSet.dstBinding = 0;
        writeSet.dstArrayElement = 0;
        writeSet.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writeSet.descriptorCount = material::MAX_MATERIAL_TEXTURES;
        writeSet.pImageInfo = imageInfos.data();

        device.getLogicalDevice().updateDescriptorSets(writeSet, nullptr);
    }

    void StaticMeshPipeline::beginRenderPass(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        vk::ImageView colorView = offscreenResources.colorImages[imageIndex].colorImageView;
        vk::ImageView depthView = offscreenResources.depthImage.depthImageView;

        core::DynamicRenderingInfo info{};
        info.extent = swapChain.getSwapchainExtent();
        info.colorAttachments = { core::colorLoad(colorView) };
        info.depthAttachment = core::depthClear(depthView);

        core::beginDynamicRendering(commandBuffer, info);
    }

    void StaticMeshPipeline::beginRenderPassForSecondary(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        // Dynamic rendering with secondary command buffers uses VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT
        vk::ImageView colorView = offscreenResources.colorImages[imageIndex].colorImageView;
        vk::ImageView depthView = offscreenResources.depthImage.depthImageView;

        auto colorAttach = core::colorLoad(colorView);
        auto depthAttach = core::depthClear(depthView);

        vk::RenderingInfo renderingInfo{};
        renderingInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderingInfo.renderArea.extent = swapChain.getSwapchainExtent();
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttach;
        renderingInfo.pDepthAttachment = &depthAttach;
        renderingInfo.flags = vk::RenderingFlagBits::eContentsSecondaryCommandBuffers;

        commandBuffer.beginRendering(renderingInfo);
    }

    void StaticMeshPipeline::beginVFXRenderPass(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        vk::ImageView colorView = offscreenResources.colorImages[imageIndex].colorImageView;
        vk::ImageView depthView = offscreenResources.depthImage.depthImageView;

        core::DynamicRenderingInfo info{};
        info.extent = swapChain.getSwapchainExtent();
        info.colorAttachments = { core::colorLoad(colorView) };
        info.depthAttachment = core::depthReadOnly(depthView);

        core::beginDynamicRendering(commandBuffer, info);
    }

    void StaticMeshPipeline::beginWaterContinuePass(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        vk::ImageView colorView = offscreenResources.colorImages[imageIndex].colorImageView;
        vk::ImageView depthView = offscreenResources.depthImage.depthImageView;

        core::DynamicRenderingInfo info{};
        info.extent = swapChain.getSwapchainExtent();
        info.colorAttachments = { core::colorLoad(colorView) };
        info.depthAttachment = core::depthLoad(depthView);

        core::beginDynamicRendering(commandBuffer, info);
        // Water continue pass runs post-resolve into the single-sample scene color.
        commandBuffer.setRasterizationSamplesEXT(vk::SampleCountFlagBits::e1);
    }

    void StaticMeshPipeline::endRenderPass(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        core::endDynamicRendering(commandBuffer);

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImage,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);
    }

    // ---- Graph-managed variants (no scene color transitions) ----

    void StaticMeshPipeline::recordCommandBufferGraphManaged(const vk::CommandBuffer& commandBuffer,
                                                              uint32_t imageIndex,
                                                              const std::vector<MeshRenderData>& meshDrawList,
                                                              const math::Frustum* frustum,
                                                              render::DebugRenderer* debugRenderer,
                                                              const glm::mat4& debugView,
                                                              const glm::mat4& debugProjection) const
    {
        bool hasDebugItems = debugRenderer && debugRenderer->hasItemsToRender();
        if (meshDrawList.empty() && !hasDebugItems)
            return;

        if (!meshDrawList.empty())
            prepareTexturesForFrame(meshDrawList);

        auto cacheLock = materialCacheManager->acquireSharedLock();
        const auto& materialCache = materialCacheManager->getCache();

        beginRenderPassGraphManaged(commandBuffer, imageIndex);

        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                         pipelineLayout, 0, descriptorSet, nullptr);

        vk::DescriptorSet frameTextureDescriptorSet = getTextureDescriptorSet(imageIndex);
        if (textureDescriptorsInitialized && frameTextureDescriptorSet)
        {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                             pipelineLayout, 1, frameTextureDescriptorSet, nullptr);
        }

        std::vector<SortedSubmesh> opaqueSubmeshes;
        std::vector<SortedSubmesh> maskedSubmeshes;
        collectSortedSubmeshes(meshDrawList, frustum, materialCache, opaqueSubmeshes, maskedSubmeshes);

        RenderState state;
        state.currentMaterialDescriptorSet = frameTextureDescriptorSet;
        state.defaultMaterialDescriptorSet = frameTextureDescriptorSet;

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        state.currentPipeline = graphicsPipeline;

        for (const auto& item : opaqueSubmeshes)
        {
            renderSubmesh(commandBuffer, *item.meshData, *item.subMesh, item.subMeshIndex,
                         material::BlendMode::Opaque, materialCache, state);
        }

        for (const auto& item : maskedSubmeshes)
        {
            renderSubmesh(commandBuffer, *item.meshData, *item.subMesh, item.subMeshIndex,
                         material::BlendMode::Masked, materialCache, state);
        }

        if (debugRenderer && debugRenderer->hasItemsToRender())
        {
            debugRenderer->render(commandBuffer, meshDrawList, debugView, debugProjection,
                                  [this](const std::string& meshId) { return getMesh(meshId); });
        }

        endRenderPassGraphManaged(commandBuffer, imageIndex);
    }

    void StaticMeshPipeline::beginRenderPassGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        const bool msaa = offscreenResources.msaaEnabled();

        vk::ImageView colorView = msaa ? offscreenResources.colorImagesMSAA[imageIndex].colorImageView
                                       : offscreenResources.colorImages[imageIndex].colorImageView;
        vk::ImageView depthView = msaa ? offscreenResources.depthImageMSAA.depthImageView
                                       : offscreenResources.depthImage.depthImageView;
        vk::ImageView resolveColor = msaa ? offscreenResources.colorImages[imageIndex].colorImageView : nullptr;
        vk::ImageView resolveDepth = msaa ? offscreenResources.depthImage.depthImageView : nullptr;

        core::DynamicRenderingInfo info{};
        info.extent = swapChain.getSwapchainExtent();
        info.colorAttachments = { core::colorLoad(colorView, resolveColor) };
        info.depthAttachment = core::depthClear(depthView, 1.0f, 0, resolveDepth);

        core::beginDynamicRendering(commandBuffer, info);
        // Dynamic MSAA: opaque geometry renders at the scene target's sample count.
        commandBuffer.setRasterizationSamplesEXT(offscreenResources.sampleCount);
    }

    void StaticMeshPipeline::beginRenderPassForSecondaryGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        const bool msaa = offscreenResources.msaaEnabled();

        // Under MSAA, render into the multisampled targets and resolve into the
        // single-sample colorImages/depthImage at store time.
        vk::ImageView colorView = msaa ? offscreenResources.colorImagesMSAA[imageIndex].colorImageView
                                       : offscreenResources.colorImages[imageIndex].colorImageView;
        vk::ImageView depthView = msaa ? offscreenResources.depthImageMSAA.depthImageView
                                       : offscreenResources.depthImage.depthImageView;
        vk::ImageView resolveColor = msaa ? offscreenResources.colorImages[imageIndex].colorImageView : nullptr;
        vk::ImageView resolveDepth = msaa ? offscreenResources.depthImage.depthImageView : nullptr;

        auto colorAttach = core::colorLoad(colorView, resolveColor);
        auto depthAttach = core::depthClear(depthView, 1.0f, 0, resolveDepth);

        std::vector<vk::RenderingAttachmentInfo> colorAttachments = { colorAttach };

        // MRT motion vectors disabled for now — using fullscreen compute motion vector pass instead

        vk::RenderingInfo renderingInfo{};
        renderingInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderingInfo.renderArea.extent = swapChain.getSwapchainExtent();
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
        renderingInfo.pColorAttachments = colorAttachments.data();
        renderingInfo.pDepthAttachment = &depthAttach;
        renderingInfo.flags = vk::RenderingFlagBits::eContentsSecondaryCommandBuffers;

        commandBuffer.beginRendering(renderingInfo);
    }

    void StaticMeshPipeline::beginVFXRenderPassGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);

        vk::ImageView colorView = offscreenResources.colorImages[imageIndex].colorImageView;
        vk::ImageView depthView = offscreenResources.depthImage.depthImageView;

        core::DynamicRenderingInfo info{};
        info.extent = swapChain.getSwapchainExtent();
        info.colorAttachments = { core::colorLoad(colorView) };
        info.depthAttachment = core::depthReadOnly(depthView);

        core::beginDynamicRendering(commandBuffer, info);
    }

    void StaticMeshPipeline::restoreDepthAfterVFX(const vk::CommandBuffer& commandBuffer) const
    {
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);
    }

    void StaticMeshPipeline::beginWaterContinuePassGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        vk::ImageView colorView = offscreenResources.colorImages[imageIndex].colorImageView;
        vk::ImageView depthView = offscreenResources.depthImage.depthImageView;

        core::DynamicRenderingInfo info{};
        info.extent = swapChain.getSwapchainExtent();
        info.colorAttachments = { core::colorLoad(colorView) };
        info.depthAttachment = core::depthLoad(depthView);

        core::beginDynamicRendering(commandBuffer, info);
        // Water continue pass runs post-resolve into the single-sample scene color.
        commandBuffer.setRasterizationSamplesEXT(vk::SampleCountFlagBits::e1);
    }

    void StaticMeshPipeline::endRenderPassGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t /*imageIndex*/) const
    {
        core::endDynamicRendering(commandBuffer);
    }
}
