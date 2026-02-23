#include "VFXMeshPreviewPipeline.hpp"
#include "../mesh/MeshGPUCache.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Texture.hpp"
#include "print/Logger.hpp"
#include <filesystem>

namespace render::vfx
{
    void VFXMeshPreviewPipeline::updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                                   const glm::vec3& cameraPos, float time) const
    {
        if (!cameraUBOMapped)
        {
            return;
        }

        VFXCameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;
        ubo.time = time;

        std::memcpy(cameraUBOMapped, &ubo, sizeof(ubo));
    }

    void VFXMeshPreviewPipeline::setParticleInstances(const std::vector<VFXInstanceData>& instances)
    {
        if (instances.empty())
        {
            currentInstanceCount = 0;
            return;
        }

        currentInstanceCount = static_cast<uint32_t>(std::min(instances.size(),
                                                              static_cast<size_t>(maxInstances)));

        vk::DeviceSize bufferSize = sizeof(VFXInstanceData) * currentInstanceCount;
        std::memcpy(instanceBufferMapped, instances.data(), bufferSize);
    }

    void VFXMeshPreviewPipeline::setTexture(const std::string& texturePath)
    {
        if (texturePath == currentTexturePath)
        {
            return;
        }

        device.getLogicalDevice().waitIdle();

        customTexture.reset();
        currentTexturePath.clear();

        if (texturePath.empty() || !std::filesystem::exists(texturePath))
        {
            if (!texturePath.empty())
            {
                loggerWarning("VFX mesh preview texture not found: {}", texturePath);
            }
            updateDescriptorSet();
            return;
        }

        try
        {
            customTexture = std::make_unique<core::Texture>(device);
            customTexture->loadTextureFromFile(texturePath, vk::Format::eR8G8B8A8Srgb, false);
            currentTexturePath = texturePath;
        }
        catch (const std::exception& e)
        {
            loggerError("Failed to load VFX mesh preview texture '{}': {}", texturePath, e.what());
            customTexture.reset();
            currentTexturePath.clear();
        }

        updateDescriptorSet();
    }

    void VFXMeshPreviewPipeline::setMesh(const std::string& meshPath)
    {
        if (meshPath == currentMeshPath)
        {
            return;
        }

        device.getLogicalDevice().waitIdle();

        currentMeshPath = meshPath;
        meshVertexBuffer = nullptr;
        meshIndexBuffer = nullptr;
        meshIndexCount = 0;
        currentMeshId.clear();

        if (meshPath.empty())
        {
            return;
        }

        std::string meshId = meshCache.loadMesh(meshPath);
        if (meshId.empty())
        {
            loggerWarning("VFXMeshPreviewPipeline: Failed to load mesh: {}", meshPath);
            return;
        }

        const auto* meshData = meshCache.getMesh(meshId);
        if (!meshData || meshData->subMeshes.empty())
        {
            loggerWarning("VFXMeshPreviewPipeline: No submeshes in mesh: {}", meshPath);
            return;
        }

        const auto& lod0 = meshData->subMeshes[0].getLOD(0);
        if (!lod0.isValid())
        {
            loggerWarning("VFXMeshPreviewPipeline: Invalid LOD 0 for mesh: {}", meshPath);
            return;
        }

        currentMeshId = meshId;
        meshVertexBuffer = lod0.vertexBuffer;
        meshIndexBuffer = lod0.indexBuffer;
        meshIndexCount = lod0.indexCount;

        loggerInfo("VFXMeshPreviewPipeline: Mesh set: {} ({} indices)", meshPath, meshIndexCount);
    }

    void VFXMeshPreviewPipeline::setRenderingConfig(float alphaClipThreshold, bool additiveBlend,
                                                       const glm::vec3& glowColor,
                                                       float uvScrollSpeedU, float uvScrollSpeedV)
    {
        pushConstants.alphaClipThreshold = alphaClipThreshold;
        pushConstants.blendMode = additiveBlend ? 1u : 0u;
        pushConstants.glowColorR = glowColor.r;
        pushConstants.glowColorG = glowColor.g;
        pushConstants.glowColorB = glowColor.b;
        pushConstants.uvScrollSpeedU = uvScrollSpeedU;
        pushConstants.uvScrollSpeedV = uvScrollSpeedV;
    }

    void VFXMeshPreviewPipeline::recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                                       uint32_t imageIndex) const
    {
        if (!initialized)
        {
            return;
        }

        std::array<vk::ClearValue, 2> clearValues{};
        clearValues[0].color = vk::ClearColorValue{std::array<float, 4>{0.1f, 0.1f, 0.1f, 1.0f}};
        clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

        vk::RenderPassBeginInfo renderPassInfo{};
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[imageIndex];
        renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderPassInfo.renderArea.extent = swapChain.getSwapchainExtent();
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        if (currentInstanceCount > 0 && meshIndexCount > 0 && meshVertexBuffer && meshIndexBuffer)
        {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                              0, descriptorSet, nullptr);

            vk::Buffer vertexBuffers[] = {meshVertexBuffer, instanceBuffer};
            vk::DeviceSize offsets[] = {0, 0};
            commandBuffer.bindVertexBuffers(0, 2, vertexBuffers, offsets);

            commandBuffer.bindIndexBuffer(meshIndexBuffer, 0, vk::IndexType::eUint32);

            commandBuffer.pushConstants(pipelineLayout,
                                        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                        0, sizeof(VFXMeshPreviewPushConstants), &pushConstants);

            commandBuffer.drawIndexed(meshIndexCount, currentInstanceCount, 0, 0, 0);
        }

        commandBuffer.endRenderPass();
    }
}
