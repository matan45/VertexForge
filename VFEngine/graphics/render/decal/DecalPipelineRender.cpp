#include "DecalPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/Texture.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"
#include <algorithm>

namespace render::decal
{
    void DecalPipeline::createDescriptorResources()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Set 0: camera UBO, depth texture, decal data SSBO
        {
            std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};
            bindings[0] = {0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment};
            bindings[1] = {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};
            bindings[2] = {2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment};

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();
            globalDescriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

            std::array<vk::DescriptorPoolSize, 3> poolSizes = {{
                {vk::DescriptorType::eUniformBuffer, 1},
                {vk::DescriptorType::eCombinedImageSampler, 1},
                {vk::DescriptorType::eStorageBuffer, 1}
            }};

            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.maxSets = 1;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            globalDescriptorPool = vkDevice.createDescriptorPool(poolInfo);

            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = globalDescriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &globalDescriptorSetLayout;
            globalDescriptorSet = vkDevice.allocateDescriptorSets(allocInfo)[0];

            updateGlobalDescriptorSet();
        }

        // Set 1: per-decal textures (albedo, normal, ORM)
        {
            std::array<vk::DescriptorSetLayoutBinding, 3> texBindings{};
            texBindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};
            texBindings[1] = {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};
            texBindings[2] = {2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(texBindings.size());
            layoutInfo.pBindings = texBindings.data();
            textureDescriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

            vk::DescriptorPoolSize poolSize{vk::DescriptorType::eCombinedImageSampler, 3 * (MAX_CACHED_TEXTURES + 1)};
            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.maxSets = MAX_CACHED_TEXTURES + 1;
            poolInfo.poolSizeCount = 1;
            poolInfo.pPoolSizes = &poolSize;
            poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
            textureDescriptorPool = vkDevice.createDescriptorPool(poolInfo);

            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = textureDescriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &textureDescriptorSetLayout;
            fallbackDescriptorSet = vkDevice.allocateDescriptorSets(allocInfo)[0];

            std::array<vk::DescriptorImageInfo, 3> imgInfos{};
            imgInfos[0] = {textureSampler, fallbackWhiteTexture->getImageView(), vk::ImageLayout::eShaderReadOnlyOptimal};
            imgInfos[1] = {textureSampler, fallbackNormalTexture->getImageView(), vk::ImageLayout::eShaderReadOnlyOptimal};
            imgInfos[2] = {textureSampler, fallbackWhiteTexture->getImageView(), vk::ImageLayout::eShaderReadOnlyOptimal};

            std::array<vk::WriteDescriptorSet, 3> writes{};
            for (uint32_t i = 0; i < 3; ++i)
            {
                writes[i].dstSet = fallbackDescriptorSet;
                writes[i].dstBinding = i;
                writes[i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
                writes[i].descriptorCount = 1;
                writes[i].pImageInfo = &imgInfos[i];
            }
            vkDevice.updateDescriptorSets(writes, {});
        }
    }

    void DecalPipeline::updateGlobalDescriptorSet()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorBufferInfo cameraBufferInfo{cameraUBOBuffer, 0, sizeof(CameraUBO)};
        vk::DescriptorImageInfo depthImageInfo{depthSampler, offscreenResources.depthImage.depthImageView,
                                                vk::ImageLayout::eDepthStencilReadOnlyOptimal};
        vk::DescriptorBufferInfo decalBufferInfo{decalDataBuffer, 0, sizeof(DecalGPUData) * maxDecals};

        std::array<vk::WriteDescriptorSet, 3> writes{};
        writes[0].dstSet = globalDescriptorSet; writes[0].dstBinding = 0;
        writes[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[0].descriptorCount = 1; writes[0].pBufferInfo = &cameraBufferInfo;

        writes[1].dstSet = globalDescriptorSet; writes[1].dstBinding = 1;
        writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[1].descriptorCount = 1; writes[1].pImageInfo = &depthImageInfo;

        writes[2].dstSet = globalDescriptorSet; writes[2].dstBinding = 2;
        writes[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[2].descriptorCount = 1; writes[2].pBufferInfo = &decalBufferInfo;

        vkDevice.updateDescriptorSets(writes, {});
    }

    core::Texture* DecalPipeline::getOrLoadTexture(const std::string& path, vk::Format format)
    {
        auto it = textureCache.find(path);
        if (it != textureCache.end()) return it->second.get();

        try
        {
            auto tex = std::make_unique<core::Texture>(device);
            tex->loadTextureFromFile(path, format, false);
            auto* ptr = tex.get();
            textureCache[path] = std::move(tex);
            return ptr;
        }
        catch (const std::exception& e)
        {
            vfLogError("DecalPipeline: Failed to load texture '{}': {}", path, e.what());
            return nullptr;
        }
    }

    vk::DescriptorSet DecalPipeline::getOrCreateDecalTextureSet(
        const std::string& albedoPath, const std::string& normalPath, const std::string& ormPath)
    {
        std::string key = albedoPath + "|" + normalPath + "|" + ormPath;
        auto it = decalDescriptorCache.find(key);
        if (it != decalDescriptorCache.end()) return it->second;

        if (decalDescriptorCache.size() >= MAX_CACHED_TEXTURES)
        {
            vfLogWarning("DecalPipeline: descriptor cache full, using fallback");
            return fallbackDescriptorSet;
        }

        core::Texture* albedoTex = albedoPath.empty() ? fallbackWhiteTexture.get() : getOrLoadTexture(albedoPath, vk::Format::eR8G8B8A8Srgb);
        core::Texture* normalTex = normalPath.empty() ? fallbackNormalTexture.get() : getOrLoadTexture(normalPath, vk::Format::eR8G8B8A8Unorm);
        core::Texture* ormTex = ormPath.empty() ? fallbackWhiteTexture.get() : getOrLoadTexture(ormPath, vk::Format::eR8G8B8A8Unorm);

        if (!albedoTex) albedoTex = fallbackWhiteTexture.get();
        if (!normalTex) normalTex = fallbackNormalTexture.get();
        if (!ormTex) ormTex = fallbackWhiteTexture.get();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = textureDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &textureDescriptorSetLayout;
        vk::DescriptorSet descSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        std::array<vk::DescriptorImageInfo, 3> imgInfos{};
        imgInfos[0] = {textureSampler, albedoTex->getImageView(), vk::ImageLayout::eShaderReadOnlyOptimal};
        imgInfos[1] = {textureSampler, normalTex->getImageView(), vk::ImageLayout::eShaderReadOnlyOptimal};
        imgInfos[2] = {textureSampler, ormTex->getImageView(), vk::ImageLayout::eShaderReadOnlyOptimal};

        std::array<vk::WriteDescriptorSet, 3> writes{};
        for (uint32_t i = 0; i < 3; ++i)
        {
            writes[i].dstSet = descSet;
            writes[i].dstBinding = i;
            writes[i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo = &imgInfos[i];
        }
        device.getLogicalDevice().updateDescriptorSets(writes, {});

        decalDescriptorCache[key] = descSet;
        return descSet;
    }

    void DecalPipeline::updateDecals(const std::vector<services::DecalRenderData>& decals)
    {
        currentDecals = decals;

        std::stable_sort(currentDecals.begin(), currentDecals.end(),
            [](const services::DecalRenderData& a, const services::DecalRenderData& b) {
                return a.sortPriority < b.sortPriority;
            });

        gpuDecalData.resize(currentDecals.size());
        for (size_t i = 0; i < currentDecals.size(); ++i)
        {
            const auto& decal = currentDecals[i];
            auto& gpu = gpuDecalData[i];

            gpu.inverseDecalMatrix = decal.inverseWorldMatrix;
            gpu.color = decal.color;
            gpu.fadeParams = glm::vec4(decal.angleFadeStart, decal.angleFadeEnd, decal.edgeFalloff, decal.normalStrength);
            gpu.textureFlags = glm::vec4(
                decal.albedoTexture.empty() ? 0.0f : 1.0f,
                (!decal.normalTexture.empty() && decal.modifyNormals) ? 1.0f : 0.0f,
                decal.ormTexture.empty() ? 0.0f : 1.0f, 0.0f);
        }
    }

    void DecalPipeline::setCameraData(const glm::mat4& view, const glm::mat4& projection,
                                       float nearPlane, float farPlane)
    {
        currentViewProjection = projection * view;
        currentInverseViewProjection = glm::inverse(currentViewProjection);
        currentNearPlane = nearPlane;
        currentFarPlane = farPlane;
    }

    void DecalPipeline::uploadDecalData()
    {
        if (gpuDecalData.empty()) return;
        uint32_t count = std::min(static_cast<uint32_t>(gpuDecalData.size()), maxDecals);
        vk::Device vkDevice = device.getLogicalDevice();
        void* mapped = vkDevice.mapMemory(decalDataMemory, 0, sizeof(DecalGPUData) * count);
        memcpy(mapped, gpuDecalData.data(), sizeof(DecalGPUData) * count);
        vkDevice.unmapMemory(decalDataMemory);
    }

    void DecalPipeline::uploadCameraUBO()
    {
        auto extent = swapChain.getSwapchainExtent();
        CameraUBO ubo{};
        ubo.viewProjection = currentViewProjection;
        ubo.inverseViewProjection = currentInverseViewProjection;
        ubo.cameraParams = glm::vec4(currentNearPlane, currentFarPlane,
                                      static_cast<float>(extent.width), static_cast<float>(extent.height));

        vk::Device vkDevice = device.getLogicalDevice();
        void* mapped = vkDevice.mapMemory(cameraUBOMemory, 0, sizeof(CameraUBO));
        memcpy(mapped, &ubo, sizeof(CameraUBO));
        vkDevice.unmapMemory(cameraUBOMemory);
    }

    void DecalPipeline::transitionDepthToReadOnly(const vk::CommandBuffer& cmd)
    {
        vk::ImageMemoryBarrier depthBarrier{};
        depthBarrier.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthBarrier.newLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.image = offscreenResources.depthImage.depthImage;
        depthBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
        depthBarrier.subresourceRange.baseMipLevel = 0;
        depthBarrier.subresourceRange.levelCount = 1;
        depthBarrier.subresourceRange.baseArrayLayer = 0;
        depthBarrier.subresourceRange.layerCount = 1;
        depthBarrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        depthBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eLateFragmentTests,
                            vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, depthBarrier);
    }

    void DecalPipeline::transitionDepthToAttachment(const vk::CommandBuffer& cmd)
    {
        vk::ImageMemoryBarrier depthBarrier{};
        depthBarrier.oldLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        depthBarrier.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.image = offscreenResources.depthImage.depthImage;
        depthBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
        depthBarrier.subresourceRange.baseMipLevel = 0;
        depthBarrier.subresourceRange.levelCount = 1;
        depthBarrier.subresourceRange.baseArrayLayer = 0;
        depthBarrier.subresourceRange.layerCount = 1;
        depthBarrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        depthBarrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                            vk::PipelineStageFlagBits::eEarlyFragmentTests, {}, {}, {}, depthBarrier);
    }

    void DecalPipeline::render(const vk::CommandBuffer& cmd, uint32_t imageIndex)
    {
        if (!initialized || currentDecals.empty() || !pipeline) return;

        uploadDecalData();
        uploadCameraUBO();
        transitionDepthToReadOnly(cmd);

        auto extent = swapChain.getSwapchainExtent();

        vk::RenderPassBeginInfo rpBegin{};
        rpBegin.renderPass = decalRenderPass;
        rpBegin.framebuffer = decalFramebuffers[imageIndex];
        rpBegin.renderArea.extent = extent;

        cmd.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

        vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(extent.width),
                              static_cast<float>(extent.height), 0.0f, 1.0f};
        cmd.setViewport(0, viewport);
        vk::Rect2D scissor{{0, 0}, extent};
        cmd.setScissor(0, scissor);

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, globalDescriptorSet, {});

        vk::DeviceSize offset = 0;
        cmd.bindVertexBuffers(0, cubeVertexBuffer, offset);
        cmd.bindIndexBuffer(cubeIndexBuffer, 0, vk::IndexType::eUint32);

        uint32_t count = std::min(static_cast<uint32_t>(currentDecals.size()), maxDecals);
        for (uint32_t i = 0; i < count; ++i)
        {
            vk::DescriptorSet texDescSet = getOrCreateDecalTextureSet(
                currentDecals[i].albedoTexture, currentDecals[i].normalTexture, currentDecals[i].ormTexture);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 1, texDescSet, {});

            DecalPushConstants pc{};
            pc.decalWorldMatrix = currentDecals[i].worldMatrix;
            pc.decalIndex = i;
            cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                              0, sizeof(DecalPushConstants), &pc);

            cmd.drawIndexed(cubeIndexCount, 1, 0, 0, 0);
        }

        cmd.endRenderPass();
        transitionDepthToAttachment(cmd);
    }
}
