#include "VFXDistortionPipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/Texture.hpp"
#include "../../../core/DeferredDeletionQueue.hpp"
#include "../bindless/VFXBindlessTextures.hpp"
#include "print/Log.hpp"
#include <filesystem>

namespace render::vfx
{
    VFXDistortionPipeline::VFXDistortionPipeline(core::Device& device, core::SwapChain& swapChain)
        : device(device), swapChain(swapChain)
    {
    }

    VFXDistortionPipeline::~VFXDistortionPipeline()
    {
        cleanup();
    }

    void VFXDistortionPipeline::init(vk::Format colorFmt, vk::Format depthFmt)
    {
        colorFormat = colorFmt;
        depthFormat = depthFmt;
        loadShader();
        createDescriptorSetLayout();
        createDescriptorPool();
        allocateDescriptorSet();
        createBuffers();
        createDefaultTexture();
        createSampler();
        createDepthSampler();
        createPipeline();
        initialized = true;
    }

    void VFXDistortionPipeline::recreate(vk::Format colorFmt, vk::Format depthFmt)
    {
        cleanup();
        init(colorFmt, depthFmt);
    }

    void VFXDistortionPipeline::cleanup()
    {
        if (!initialized) return;

        auto vkDevice = device.getLogicalDevice();

        emitterConfigs.clear();

        if (graphicsPipeline) { vkDevice.destroyPipeline(graphicsPipeline); graphicsPipeline = nullptr; }
        if (pipelineLayout) { vkDevice.destroyPipelineLayout(pipelineLayout); pipelineLayout = nullptr; }
        if (descriptorPool) { vkDevice.destroyDescriptorPool(descriptorPool); descriptorPool = nullptr; }
        if (descriptorSetLayout) { vkDevice.destroyDescriptorSetLayout(descriptorSetLayout); descriptorSetLayout = nullptr; }

        if (textureSampler) { vkDevice.destroySampler(textureSampler); textureSampler = nullptr; }
        if (depthSampler) { vkDevice.destroySampler(depthSampler); depthSampler = nullptr; }

        if (defaultTextureImageView) { vkDevice.destroyImageView(defaultTextureImageView); defaultTextureImageView = nullptr; }
        if (defaultTextureImage) { vkDevice.destroyImage(defaultTextureImage); defaultTextureImage = nullptr; }
        if (defaultTextureAllocation) { device.getMemoryManager().free(defaultTextureAllocation); defaultTextureAllocation = {}; }

        if (cameraUBO) { vkDevice.destroyBuffer(cameraUBO); cameraUBO = nullptr; }
        if (cameraUBOAllocation) { device.getMemoryManager().free(cameraUBOAllocation); cameraUBOAllocation = {}; }
        cameraUBOMapped = nullptr;

        renderDataMapped = nullptr;
        if (renderDataBuffer) { vkDevice.destroyBuffer(renderDataBuffer); renderDataBuffer = nullptr; }
        if (renderDataBufferAllocation) { device.getMemoryManager().free(renderDataBufferAllocation); renderDataBufferAllocation = {}; }

        if (quadVertexBuffer) { vkDevice.destroyBuffer(quadVertexBuffer); quadVertexBuffer = nullptr; }
        if (quadVertexBufferAllocation) { device.getMemoryManager().free(quadVertexBufferAllocation); quadVertexBufferAllocation = {}; }
        if (quadIndexBuffer) { vkDevice.destroyBuffer(quadIndexBuffer); quadIndexBuffer = nullptr; }
        if (quadIndexBufferAllocation) { device.getMemoryManager().free(quadIndexBufferAllocation); quadIndexBufferAllocation = {}; }

        distortionShader.reset();
        initialized = false;
    }

    void VFXDistortionPipeline::updateCameraUBO(
        const glm::mat4& view, const glm::mat4& projection,
        const glm::vec3& cameraPos, float time,
        float nearPlane, float farPlane) const
    {
        if (!cameraUBOMapped) return;

        GPUVFXCameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;
        ubo.time = time;
        ubo.nearPlane = nearPlane;
        ubo.farPlane = farPlane;

        std::memcpy(cameraUBOMapped, &ubo, sizeof(ubo));
    }

    void VFXDistortionPipeline::setSceneDepthImageView(vk::ImageView depthView)
    {
        if (sceneDepthImageView != depthView)
        {
            sceneDepthImageView = depthView;
            descriptorsNeedUpdate = true;
        }
    }

    void VFXDistortionPipeline::updateParticleBuffer(vk::Buffer particleBuffer, vk::DeviceSize particleBufferSize)
    {
        if (particleBuffer != cachedParticleBuffer || particleBufferSize != cachedParticleBufferSize)
        {
            cachedParticleBuffer = particleBuffer;
            cachedParticleBufferSize = particleBufferSize;
            descriptorsNeedUpdate = true;
        }
    }

    void VFXDistortionPipeline::updateConfigBuffer(vk::Buffer configBuffer, vk::DeviceSize configBufferSize)
    {
        if (configBuffer != cachedConfigBuffer || configBufferSize != cachedConfigBufferSize)
        {
            cachedConfigBuffer = configBuffer;
            cachedConfigBufferSize = configBufferSize;
            descriptorsNeedUpdate = true;
        }
    }

    void VFXDistortionPipeline::writeDescriptors() const
    {
        if (!descriptorsNeedUpdate || !cachedParticleBuffer || !cachedConfigBuffer) return;

        writeDescriptorSet(defaultDescriptorSet);

        descriptorsNeedUpdate = false;
    }

    void VFXDistortionPipeline::writeDescriptorSet(vk::DescriptorSet dstSet) const
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

        vk::DescriptorBufferInfo renderDataInfo{};
        renderDataInfo.buffer = renderDataBuffer;
        renderDataInfo.offset = 0;
        renderDataInfo.range = sizeof(VFXDistortionRenderData) * GPUVFXConstants::MAX_EMITTERS;

        // VK-1481: binding 1 (per-emitter distortion texture) is gone; the texture lives in the bindless set.
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

        // VK-1481 Phase 2: per-emitter distortion render-data SSBO
        writes[4].dstSet = dstSet;
        writes[4].dstBinding = 8;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[4].pBufferInfo = &renderDataInfo;

        vkDevice.updateDescriptorSets(writes, {});
    }

    void VFXDistortionPipeline::setEmitterDistortionTexture(uint32_t emitterIndex, const std::string& texturePath)
    {
        auto& config = emitterConfigs[emitterIndex];
        const std::string oldPath = config.distortionTexturePath;

        if (oldPath == texturePath) return;

        config.distortionTexturePath = texturePath;

        // VK-1481: the shared bindless table handles dedup, refcount, cap, and (deferred) teardown.
        // Distortion textures load as UNORM (normal-map/noise data), so srgb=false.
        if (!oldPath.empty() && bindless)
        {
            bindless->release(oldPath, /*srgb=*/false);
        }

        // No distortion texture (or no table wired yet): sample the neutral-normal default
        // (128,128,255) = "no displacement", NOT white.
        if (texturePath.empty() || !bindless)
        {
            config.textureIndex = bindless ? bindless->neutralNormalIndex() : 0u;
            return;
        }

        const uint32_t idx = bindless->acquire(texturePath, /*srgb=*/false);
        if (idx == bindless->defaultWhiteIndex())
        {
            // Missing / table-full / load error (already logged): fall back to the neutral-normal
            // default (not white) and drop the path so a later release() cannot over-decrement a
            // reference we never took.
            config.textureIndex = bindless->neutralNormalIndex();
            config.distortionTexturePath.clear();
            return;
        }

        config.textureIndex = idx;
    }

    void VFXDistortionPipeline::setEmitterDistortionConfig(uint32_t emitterIndex, float strength)
    {
        emitterConfigs[emitterIndex].distortionStrength = strength;
    }

    void VFXDistortionPipeline::removeEmitter(uint32_t emitterIndex)
    {
        auto configIt = emitterConfigs.find(emitterIndex);
        if (configIt != emitterConfigs.end())
        {
            if (!configIt->second.distortionTexturePath.empty() && bindless)
            {
                bindless->release(configIt->second.distortionTexturePath, /*srgb=*/false);
            }
            emitterConfigs.erase(configIt);
        }
    }

    void VFXDistortionPipeline::recordCommandsInline(
        vk::CommandBuffer cmd,
        vk::Buffer drawCommandBuffer,
        uint32_t emitterCount,
        const std::vector<bool>& distortionEnabledFlags) const
    {
        if (!initialized || emitterCount == 0 || !cachedParticleBuffer) return;

        writeDescriptors();

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        // VK-1481: set 0 (camera/particle/config/depth) is constant across the pass — bind once.
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                               0, defaultDescriptorSet, {});

        // VK-1481: shared bindless texture set (set 1) — bind once; emitters pick a slot via push constant.
        if (bindless)
        {
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                   1, bindless->getDescriptorSet(), {});
        }

        vk::Buffer vertexBuffers[] = {quadVertexBuffer};
        vk::DeviceSize offsets[] = {0};
        cmd.bindVertexBuffers(0, 1, vertexBuffers, offsets);
        cmd.bindIndexBuffer(quadIndexBuffer, 0, vk::IndexType::eUint16);

        // VK-1481 Phase 2: collect distortion-enabled slots (ascending), write their per-emitter
        // render-data, then merge into runs of CONSECUTIVE slots. Distortion has a single graphics
        // pipeline (no sortOrder, no Multiply variant), so a run is just a maximal consecutive span.
        auto* rd = static_cast<VFXDistortionRenderData*>(renderDataMapped);
        std::vector<uint32_t> drawable;
        drawable.reserve(emitterCount);

        for (uint32_t i = 0; i < emitterCount; ++i)
        {
            // Only draw emitters with distortion enabled.
            if (i >= distortionEnabledFlags.size() || !distortionEnabledFlags[i])
                continue;

            float strength = 0.1f;
            // VK-1481: the "no distortion texture" default is neutral-normal (not white). A config
            // whose textureIndex is still 0 never had a texture assigned, so keep neutral-normal.
            uint32_t texIndex = bindless ? bindless->neutralNormalIndex() : 0u;

            auto configIt = emitterConfigs.find(i);
            if (configIt != emitterConfigs.end())
            {
                strength = configIt->second.distortionStrength;
                if (configIt->second.textureIndex != 0u)
                    texIndex = configIt->second.textureIndex; // VK-1481: bindless slot, selected in-shader
            }

            if (rd)
                rd[i] = VFXDistortionRenderData{texIndex, strength, 0.0f, 0.0f};

            drawable.push_back(i);
        }

        // Emit one multiDrawIndirect per run of CONSECUTIVE slots. gl_DrawID within the multi-draw
        // recovers emitterSlot = runBaseSlot + gl_DrawID in the shader. Single pipeline — no switch.
        size_t idx = 0;
        while (idx < drawable.size())
        {
            const uint32_t baseSlot = drawable[idx];
            size_t j = idx + 1;
            while (j < drawable.size() && drawable[j] == drawable[j - 1] + 1)
            {
                ++j;
            }
            const uint32_t runLen = static_cast<uint32_t>(j - idx);

            GPUVFXMergedPushConstants pushConstants{};
            pushConstants.runBaseSlot = baseSlot;
            cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex,
                              0, sizeof(GPUVFXMergedPushConstants), &pushConstants);

            const vk::DeviceSize offset = static_cast<vk::DeviceSize>(baseSlot) * sizeof(VFXDrawIndirectCommand);
            cmd.drawIndexedIndirect(drawCommandBuffer, offset, runLen, sizeof(VFXDrawIndirectCommand));
            render::FrameDrawStats::count(render::DrawCategory::VFX);

            idx = j;
        }
    }
}
