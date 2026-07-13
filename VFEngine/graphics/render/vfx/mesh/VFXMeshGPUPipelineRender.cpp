#include "VFXMeshGPUPipeline.hpp"
#include "../../mesh/MeshGPUCache.hpp"
#include "../../mesh/MeshTypes.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Texture.hpp"
#include "../../../core/DeferredDeletionQueue.hpp"
#include "../bindless/VFXBindlessTextures.hpp"
#include "print/Log.hpp"
#include "../compute/GPUVFXTypes.hpp"
#include "vfx/VFXSortOrder.hpp"
#include <filesystem>

namespace render::vfx
{
    void VFXMeshGPUPipeline::updateCameraUBO(
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPos,
        float time,
        float nearPlane,
        float farPlane) const
    {
        if (!cameraUBOMapped)
        {
            return;
        }

        GPUVFXCameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;
        ubo.time = time;
        ubo.nearPlane = nearPlane;
        ubo.farPlane = farPlane;

        std::memcpy(cameraUBOMapped, &ubo, sizeof(ubo));
    }

    void VFXMeshGPUPipeline::setSceneDepthImageView(vk::ImageView depthView)
    {
        if (sceneDepthImageView != depthView)
        {
            sceneDepthImageView = depthView;
            descriptorsNeedUpdate = true;
        }
    }

    void VFXMeshGPUPipeline::updateParticleBuffer(vk::Buffer particleBuffer, vk::DeviceSize particleBufferSize)
    {
        if (particleBuffer != cachedParticleBuffer || particleBufferSize != cachedParticleBufferSize)
        {
            cachedParticleBuffer = particleBuffer;
            cachedParticleBufferSize = particleBufferSize;
            descriptorsNeedUpdate = true;
        }
    }

    void VFXMeshGPUPipeline::updateConfigBuffer(vk::Buffer configBuffer, vk::DeviceSize configBufferSize)
    {
        if (configBuffer != cachedConfigBuffer || configBufferSize != cachedConfigBufferSize)
        {
            cachedConfigBuffer = configBuffer;
            cachedConfigBufferSize = configBufferSize;
            descriptorsNeedUpdate = true;
        }
    }

    void VFXMeshGPUPipeline::writeDescriptors() const
    {
        if (!descriptorsNeedUpdate || !cachedParticleBuffer || !cachedConfigBuffer)
        {
            return;
        }

        writeDescriptorSet(defaultDescriptorSet);

        descriptorsNeedUpdate = false;
    }

    void VFXMeshGPUPipeline::writeDescriptorSet(vk::DescriptorSet dstSet) const
    {
        auto vkDevice = device.getLogicalDevice();

        vk::DescriptorBufferInfo cameraInfo{};
        cameraInfo.buffer = cameraUBO;
        cameraInfo.offset = 0;
        cameraInfo.range = sizeof(GPUVFXCameraUBO);

        vk::DescriptorBufferInfo particleInfo{};
        particleInfo.buffer = cachedParticleBuffer;
        particleInfo.offset = 0;
        particleInfo.range = cachedParticleBufferSize;

        vk::DescriptorBufferInfo configInfo{};
        configInfo.buffer = cachedConfigBuffer;
        configInfo.offset = 0;
        configInfo.range = cachedConfigBufferSize;

        vk::DescriptorImageInfo depthInfo{};
        depthInfo.imageView = sceneDepthImageView ? sceneDepthImageView : defaultTextureImageView;
        depthInfo.imageLayout = sceneDepthImageView
            ? vk::ImageLayout::eDepthStencilReadOnlyOptimal
            : vk::ImageLayout::eShaderReadOnlyOptimal;
        depthInfo.sampler = depthSampler ? depthSampler : textureSampler;

        // VK-1526: per-emitter mesh-material SSBO (whole range; always valid once created at init).
        vk::DescriptorBufferInfo materialInfo{};
        materialInfo.buffer = materialSlotsBuffer;
        materialInfo.offset = 0;
        materialInfo.range = static_cast<vk::DeviceSize>(GPUVFXConstants::MAX_EMITTERS) * sizeof(VFXMeshMaterialSlots);

        // VK-1481: binding 1 (per-emitter texture) is gone; the texture lives in the bindless set.
        // VK-1526: binding 5 is the per-emitter mesh-material SSBO.
        std::array<vk::WriteDescriptorSet, 5> writes{};

        writes[0].dstSet = dstSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[0].pBufferInfo = &cameraInfo;

        writes[1].dstSet = dstSet;
        writes[1].dstBinding = 2;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].pBufferInfo = &particleInfo;

        writes[2].dstSet = dstSet;
        writes[2].dstBinding = 3;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[2].pBufferInfo = &configInfo;

        writes[3].dstSet = dstSet;
        writes[3].dstBinding = 4;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[3].pImageInfo = &depthInfo;

        writes[4].dstSet = dstSet;
        writes[4].dstBinding = 5;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[4].pBufferInfo = &materialInfo;

        vkDevice.updateDescriptorSets(writes, {});
    }

    void VFXMeshGPUPipeline::setEmitterMesh(uint32_t emitterIndex, const std::string& meshPath)
    {
        if (meshPath.empty())
        {
            emitterMeshes.erase(emitterIndex);
            return;
        }

        std::string meshId = meshCache.loadMesh(meshPath);
        if (meshId.empty())
        {
            vfLogWarning("VFXMeshGPUPipeline: Failed to load mesh: {}", meshPath);
            return;
        }

        const auto* meshData = meshCache.getMesh(meshId);
        if (!meshData || meshData->subMeshes.empty())
        {
            vfLogWarning("VFXMeshGPUPipeline: No submeshes in mesh: {}", meshPath);
            return;
        }

        const auto& lod0 = meshData->subMeshes[0].getLOD(0);
        if (!lod0.isValid())
        {
            vfLogWarning("VFXMeshGPUPipeline: Invalid LOD 0 for mesh: {}", meshPath);
            return;
        }

        EmitterMeshData& emMesh = emitterMeshes[emitterIndex];
        emMesh.meshPath = meshPath;
        emMesh.meshId = meshId;
        emMesh.vertexBuffer = lod0.vertexBuffer;
        emMesh.indexBuffer = lod0.indexBuffer;
        emMesh.indexCount = lod0.indexCount;

        vfLogInfo("VFXMeshGPUPipeline: Mesh set for emitter {}: {} ({} indices)",
                   emitterIndex, meshPath, lod0.indexCount);
    }

    void VFXMeshGPUPipeline::setEmitterTexture(uint32_t emitterIndex, const std::string& texturePath)
    {
        auto& config = emitterConfigs[emitterIndex];
        const std::string oldPath = config.texturePath;

        if (oldPath == texturePath)
        {
            return;
        }

        config.texturePath = texturePath;

        // VK-1481: the shared bindless table handles dedup, refcount, cap, and (deferred) teardown.
        if (!oldPath.empty() && bindless)
        {
            bindless->release(oldPath, /*srgb=*/true);
        }

        if (texturePath.empty() || !bindless)
        {
            config.textureIndex = bindless ? bindless->defaultWhiteIndex() : 0u;
            return;
        }

        const uint32_t idx = bindless->acquire(texturePath, /*srgb=*/true);
        config.textureIndex = idx;
        if (idx == bindless->defaultWhiteIndex())
        {
            // Missing / table-full / load error (already logged): drop the path so a later
            // release() cannot over-decrement a reference we never took.
            config.texturePath.clear();
        }
    }

    void VFXMeshGPUPipeline::releaseMaterialMaps(EmitterRenderConfig& config)
    {
        // Release the exact bindless refs each map took (paths were only stored on a successful acquire),
        // with the same srgb flag used to acquire (srgb is part of the ref-table key).
        if (bindless)
        {
            if (!config.matAlbedoPath.empty())   bindless->release(config.matAlbedoPath, /*srgb=*/true);
            if (!config.matNormalPath.empty())   bindless->release(config.matNormalPath, /*srgb=*/false);
            if (!config.matOrmPath.empty())      bindless->release(config.matOrmPath, /*srgb=*/false);
            if (!config.matEmissivePath.empty()) bindless->release(config.matEmissivePath, /*srgb=*/true);
        }
        config.matAlbedoPath.clear();
        config.matNormalPath.clear();
        config.matOrmPath.clear();
        config.matEmissivePath.clear();
    }

    void VFXMeshGPUPipeline::uploadMaterialSlot(uint32_t emitterIndex, const VFXMeshMaterialSlots& slots) const
    {
        if (!materialSlotsMapped || emitterIndex >= GPUVFXConstants::MAX_EMITTERS)
        {
            return;
        }
        std::memcpy(static_cast<uint8_t*>(materialSlotsMapped) + static_cast<size_t>(emitterIndex) * sizeof(VFXMeshMaterialSlots),
                    &slots, sizeof(VFXMeshMaterialSlots));
    }

    void VFXMeshGPUPipeline::setEmitterMaterial(uint32_t emitterIndex, const ResolvedVFXMeshMaterial& resolved)
    {
        auto& config = emitterConfigs[emitterIndex];

        // Called at (re)configure time (not per-frame): release the previously-held maps, then re-acquire.
        // The bindless table's deferred teardown makes re-setting the same material safe (a re-acquire in
        // the teardown window reuses the resident texture).
        releaseMaterialMaps(config);

        VFXMeshMaterialSlots slots{}; // materialFlags == 0 => the mesh shader takes the legacy single-.vfImage path

        if (resolved.hasMaterial && bindless)
        {
            uint32_t flags = MeshMaterialFlags::HasMaterial;

            // baseColor (sRGB). A missing map => white default; the albedo tint still colors the shard.
            if (!resolved.albedoPath.empty())
            {
                const uint32_t idx = bindless->acquire(resolved.albedoPath, /*srgb=*/true);
                slots.baseColorIdx = idx;
                if (idx != bindless->defaultWhiteIndex())
                {
                    config.matAlbedoPath = resolved.albedoPath;
                }
            }

            // normal (linear). Flag HasNormal only when a real map loaded; otherwise the shader keeps the
            // geometric normal (bindless->acquire returns the white default on empty/miss, never the neutral
            // normal, so gate on the flag instead of the slot).
            if (!resolved.normalPath.empty())
            {
                const uint32_t idx = bindless->acquire(resolved.normalPath, /*srgb=*/false);
                if (idx != bindless->defaultWhiteIndex())
                {
                    slots.normalIdx = idx;
                    config.matNormalPath = resolved.normalPath;
                    flags |= MeshMaterialFlags::HasNormal;
                }
            }

            // ORM (linear, R=AO/G=Rough/B=Metal). Flag UsesORM only when a real map loaded; else the shader
            // uses the scalar metallic/roughness/ao below.
            if (resolved.usesORM && !resolved.ormPath.empty())
            {
                const uint32_t idx = bindless->acquire(resolved.ormPath, /*srgb=*/false);
                if (idx != bindless->defaultWhiteIndex())
                {
                    slots.ormIdx = idx;
                    config.matOrmPath = resolved.ormPath;
                    flags |= MeshMaterialFlags::UsesORM;
                }
            }

            // emissive (sRGB).
            if (!resolved.emissivePath.empty())
            {
                const uint32_t idx = bindless->acquire(resolved.emissivePath, /*srgb=*/true);
                if (idx != bindless->defaultWhiteIndex())
                {
                    slots.emissiveIdx = idx;
                    config.matEmissivePath = resolved.emissivePath;
                    flags |= MeshMaterialFlags::HasEmissive;
                }
            }

            slots.materialFlags = flags;
            slots.metallic = resolved.metallic;
            slots.roughness = resolved.roughness;
            slots.ao = resolved.ao;
            slots.emissionStrength = resolved.emissionStrength;
            slots.albedoTintR = resolved.albedoTint.r;
            slots.albedoTintG = resolved.albedoTint.g;
            slots.albedoTintB = resolved.albedoTint.b;
            slots.albedoTintA = resolved.albedoTint.a;
        }

        config.materialSlots = slots;
        uploadMaterialSlot(emitterIndex, slots);
    }

    void VFXMeshGPUPipeline::setEmitterRenderingConfig(uint32_t emitterIndex,
                                                         float alphaClipThreshold, ::vfx::VFXBlendMode blendMode,
                                                         const glm::vec3& glowColor, int32_t sortOrder)
    {
        emitterConfigs[emitterIndex].alphaClipThreshold = alphaClipThreshold;
        emitterConfigs[emitterIndex].blendMode = ::vfx::blendModeToGpuValue(blendMode);
        emitterConfigs[emitterIndex].glowColor = glowColor;
        emitterConfigs[emitterIndex].sortOrder = sortOrder;
    }

    void VFXMeshGPUPipeline::removeEmitter(uint32_t emitterIndex)
    {
        auto configIt = emitterConfigs.find(emitterIndex);
        if (configIt != emitterConfigs.end())
        {
            // VK-1481: drop the shared bindless reference; teardown is deferred inside the table.
            if (!configIt->second.texturePath.empty() && bindless)
            {
                bindless->release(configIt->second.texturePath, /*srgb=*/true);
            }
            // VK-1526: drop the material maps' bindless refs and zero the SSBO slot so a future emitter
            // reusing this index does not inherit stale material data.
            releaseMaterialMaps(configIt->second);
            uploadMaterialSlot(emitterIndex, VFXMeshMaterialSlots{});
            emitterConfigs.erase(configIt);
        }
        emitterMeshes.erase(emitterIndex);
    }

    uint32_t VFXMeshGPUPipeline::getEmitterMeshIndexCount(uint32_t emitterIndex) const
    {
        auto it = emitterMeshes.find(emitterIndex);
        if (it != emitterMeshes.end())
        {
            return it->second.indexCount;
        }
        return 6;
    }

    void VFXMeshGPUPipeline::recordCommandsInline(
        vk::CommandBuffer cmd,
        vk::Buffer drawCommandBuffer,
        uint32_t emitterCount) const
    {
        if (!initialized || emitterCount == 0 || !cachedParticleBuffer || emitterMeshes.empty())
        {
            return;
        }

        writeDescriptors();

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        vk::Pipeline lastBoundPipeline = graphicsPipeline; // VK-1472: swapped to Multiply variant per-emitter

        // Bind lighting descriptor sets (sets 1-3) if available
        if (lightingAvailable)
        {
            std::array<vk::DescriptorSet, 3> lightingSets = {
                cachedLightBufferSet, cachedClusterGridSet, cachedClusterLightGridSet
            };
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                   1, lightingSets, {});
        }

        // VK-1481: set 0 (camera/particle/config/depth) is constant across the pass — bind once.
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                               0, defaultDescriptorSet, {});

        // VK-1481: shared bindless texture set (set 4) — bind once; emitters pick a slot via push constant.
        if (bindless)
        {
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                   4, bindless->getDescriptorSet(), {});
        }

        // VK-1471: submit mesh-particle emitter draws in ascending sortOrder. Built in
        // the map's current traversal order, so an all-default (0) set is a stable no-op.
        std::vector<::vfx::VFXDrawOrderEntry> drawOrder;
        drawOrder.reserve(emitterMeshes.size());
        for (const auto& meshEntry : emitterMeshes)
        {
            const uint32_t emitterIdx = meshEntry.first;
            auto cfgIt = emitterConfigs.find(emitterIdx);
            const int32_t so = (cfgIt != emitterConfigs.end()) ? cfgIt->second.sortOrder : 0;
            drawOrder.push_back({emitterIdx, so});
        }
        ::vfx::stableSortDrawOrder(drawOrder);

        for (const auto& drawEntry : drawOrder)
        {
            const uint32_t emitterIdx = drawEntry.index;
            auto meshIt = emitterMeshes.find(emitterIdx);
            if (meshIt == emitterMeshes.end())
                continue;
            const auto& meshData = meshIt->second;

            if (emitterIdx >= emitterCount || meshData.indexCount == 0)
                continue;

            vk::Buffer vertexBuffers[] = {meshData.vertexBuffer};
            vk::DeviceSize offsets[] = {0};
            cmd.bindVertexBuffers(0, 1, vertexBuffers, offsets);
            cmd.bindIndexBuffer(meshData.indexBuffer, 0, vk::IndexType::eUint32);

            float alphaClip = 0.1f;
            uint32_t blendMode = 0;
            glm::vec3 gc(1.0f);
            uint32_t texIndex = bindless ? bindless->defaultWhiteIndex() : 0u;

            auto configIt = emitterConfigs.find(emitterIdx);
            if (configIt != emitterConfigs.end())
            {
                alphaClip = configIt->second.alphaClipThreshold;
                blendMode = configIt->second.blendMode;
                gc = configIt->second.glowColor;
                texIndex = configIt->second.textureIndex; // VK-1481: bindless slot, selected in-shader
            }

            // VK-1472: Multiply emitters bind the dedicated Multiply blend pipeline (shares the
            // layout, so the descriptor sets + vertex/index bindings above stay valid).
            vk::Pipeline wantPipeline = (blendMode == 3u && multiplyPipeline) ? multiplyPipeline : graphicsPipeline;
            if (wantPipeline != lastBoundPipeline)
            {
                cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, wantPipeline);
                lastBoundPipeline = wantPipeline;
            }

            GPUVFXBillboardPushConstants pushConstants{};
            pushConstants.emitterIndex = emitterIdx;
            pushConstants.alphaClipThreshold = alphaClip;
            pushConstants.blendMode = blendMode;
            pushConstants.glowColorR = gc.r;
            pushConstants.glowColorG = gc.g;
            pushConstants.glowColorB = gc.b;
            pushConstants.textureIndex = texIndex;
            cmd.pushConstants(pipelineLayout,
                              vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                              0, sizeof(GPUVFXBillboardPushConstants), &pushConstants);

            vk::DeviceSize offset = emitterIdx * sizeof(VFXDrawIndirectCommand);
            cmd.drawIndexedIndirect(drawCommandBuffer, offset, 1, sizeof(VFXDrawIndirectCommand));
            render::FrameDrawStats::count(render::DrawCategory::VFX);
        }
    }
}
