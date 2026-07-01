#include "VFXBillboardPipeline.hpp"
#include "../quad/VFXQuadData.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/OffScreen.hpp"
#include "../../../core/Texture.hpp"
#include "../../../core/DynamicRenderingHelpers.hpp"
#include "print/Log.hpp"
#include <filesystem>

namespace render::vfx
{
    void VFXBillboardPipeline::updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                                const glm::vec3& cameraPos, float time) const
    {
        VFXCameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;
        ubo.time = time;

        if (cameraUBOAllocation.mappedPtr)
        {
            std::memcpy(cameraUBOAllocation.mappedPtr, &ubo, sizeof(ubo));
        }
    }

    void VFXBillboardPipeline::setParticleInstances(const std::vector<VFXInstanceData>& instances)
    {
        if (instances.empty())
        {
            currentInstanceCount = 0;
            return;
        }

        currentInstanceCount = static_cast<uint32_t>(std::min(instances.size(),
                                                              static_cast<size_t>(maxInstances)));

        vk::DeviceSize bufferSize = sizeof(VFXInstanceData) * currentInstanceCount;
        if (instanceBufferAllocation.mappedPtr)
        {
            std::memcpy(instanceBufferAllocation.mappedPtr, instances.data(), bufferSize);
        }
    }

    void VFXBillboardPipeline::setTexture(const std::string& texturePath)
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
                vfLogWarning("VFX texture not found: {}", texturePath);
            }
            updateDescriptorSet();
            return;
        }

        try
        {
            customTexture = std::make_unique<core::Texture>(device);
            customTexture->loadTextureFromFile(texturePath, vk::Format::eR8G8B8A8Srgb, false);
            currentTexturePath = texturePath;
            vfLogInfo("VFX texture loaded: {}", texturePath);
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to load VFX texture '{}': {}", texturePath, e.what());
            customTexture.reset();
            currentTexturePath.clear();
        }

        updateDescriptorSet();
    }

    void VFXBillboardPipeline::setFlipbookConfig(const VFXFlipbookConfig& config)
    {
        flipbookPC.flipbookRows = static_cast<float>(std::max(config.rows, 1));
        flipbookPC.flipbookColumns = static_cast<float>(std::max(config.columns, 1));
        flipbookPC.alphaClipThreshold = config.alphaClipThreshold;
        flipbookPC.blendMode = config.additiveBlend ? 1u : 0u;
        flipbookPC.renderMode = static_cast<uint32_t>(config.renderMode);
        flipbookPC.stretchMultiplier = config.stretchMultiplier;
        flipbookPC.glowColorR = config.glowColor.r;
        flipbookPC.glowColorG = config.glowColor.g;
        flipbookPC.glowColorB = config.glowColor.b;
        flipbookPC.uvScrollSpeedU = config.uvScrollSpeedU;
        flipbookPC.uvScrollSpeedV = config.uvScrollSpeedV;
    }

    void VFXBillboardPipeline::recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                                    uint32_t imageIndex) const
    {
        if (!initialized)
        {
            return;
        }

        auto colorAttach = core::colorClear(
            offscreenResources.colorImages[imageIndex].colorImageView,
            vk::ClearColorValue(std::array<float, 4>{0.1f, 0.1f, 0.1f, 1.0f}));
        auto depthAttach = core::depthClear(
            offscreenResources.depthImage.depthImageView, 1.0f, 0);

        core::DynamicRenderingInfo dynInfo{};
        dynInfo.extent = swapChain.getSwapchainExtent();
        dynInfo.colorAttachments = {colorAttach};
        dynInfo.depthAttachment = depthAttach;

        core::beginDynamicRendering(commandBuffer, dynInfo);
        recordDraws(commandBuffer);
        core::endDynamicRendering(commandBuffer);
    }

    void VFXBillboardPipeline::recordDraws(const vk::CommandBuffer& commandBuffer) const
    {
        if (!initialized || currentInstanceCount == 0)
        {
            return;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                          0, descriptorSet, nullptr);

        vk::Buffer vertexBuffers[] = {quadVertexBuffer, instanceBuffer};
        vk::DeviceSize offsets[] = {0, 0};
        commandBuffer.bindVertexBuffers(0, 2, vertexBuffers, offsets);

        commandBuffer.bindIndexBuffer(quadIndexBuffer, 0, vk::IndexType::eUint16);

        commandBuffer.pushConstants(pipelineLayout,
                                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                    0, sizeof(VFXFlipbookPushConstants), &flipbookPC);

        commandBuffer.drawIndexed(VFXConstants::QUAD_INDEX_COUNT, currentInstanceCount, 0, 0, 0);
        render::FrameDrawStats::count(render::DrawCategory::VFX);
    }
}
