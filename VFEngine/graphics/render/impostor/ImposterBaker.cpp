#include "ImposterBaker.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/RenderManager.hpp"
#include "../RenderTextureViewPort.hpp"
#include "../RenderPassHandler.hpp"
#include "print/Log.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstring>

namespace render::impostor
{
    void ImposterBaker::init(core::Device& device, core::SwapChain& swapChain)
    {
        devicePtr = &device;
        swapChainPtr = &swapChain;
    }

    glm::mat4 ImposterBaker::computeOrbitalView(float horizontalAngle, float verticalAngle,
                                                   float distance, const glm::vec3& target)
    {
        float cosV = std::cos(verticalAngle);
        float sinV = std::sin(verticalAngle);
        float cosH = std::cos(horizontalAngle);
        float sinH = std::sin(horizontalAngle);

        glm::vec3 cameraPos = target + glm::vec3(
            distance * cosV * sinH,
            distance * sinV,
            distance * cosV * cosH
        );

        return glm::lookAt(cameraPos, target, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    bool ImposterBaker::readbackImage(void* srcImageRaw, uint32_t width, uint32_t height,
                                        std::vector<uint8_t>& outPixels)
    {
        vk::Image srcImage = static_cast<VkImage>(srcImageRaw);
        vk::Device vkDevice = devicePtr->getLogicalDevice();
        vk::PhysicalDevice physDevice = devicePtr->getPhysicalDevice();
        vk::CommandPool cmdPool = devicePtr->getStagingCommandPool();
        vk::Queue queue = devicePtr->getGraphicsQueue();

        vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(width) * height * 4; // RGBA8
        outPixels.resize(imageSize);

        // Create host-visible staging buffer for readback
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;

        core::BufferInfoRequest bufferReq(vkDevice, physDevice);
        bufferReq.size = imageSize;
        bufferReq.usage = vk::BufferUsageFlagBits::eTransferDst;
        bufferReq.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(bufferReq, stagingBuffer, stagingMemory);

        // Record commands: transition image → copy to buffer → transition back
        auto cmd = core::Utilities::beginSingleTimeCommands(vkDevice, cmdPool);

        // Transition from shader-read to transfer-src
        core::ImageUtilities::transitionImageLayout(cmd.get(), srcImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageAspectFlagBits::eColor);

        // Copy image to staging buffer
        vk::BufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D{0, 0, 0};
        region.imageExtent = vk::Extent3D{width, height, 1};

        cmd->copyImageToBuffer(srcImage, vk::ImageLayout::eTransferSrcOptimal, stagingBuffer, 1, &region);

        // Transition back to shader-read
        core::ImageUtilities::transitionImageLayout(cmd.get(), srcImage,
            vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::Utilities::endSingleTimeCommands(queue, cmd);

        // Map and copy
        void* mapped = vkDevice.mapMemory(stagingMemory, 0, imageSize);
        std::memcpy(outPixels.data(), mapped, imageSize);
        vkDevice.unmapMemory(stagingMemory);

        // Cleanup staging
        core::BufferUtilities::destroyBuffer(vkDevice, stagingBuffer, stagingMemory);

        return true;
    }

    ImposterBakeResult ImposterBaker::bake(const ImposterBakeRequest& request)
    {
        ImposterBakeResult result;

        if (!devicePtr || !swapChainPtr)
        {
            result.errorMessage = "ImposterBaker not initialized";
            return result;
        }

        if (!renderPassHandler)
        {
            result.errorMessage = "No RenderPassHandler set - cannot render";
            return result;
        }

        if (request.meshPath.empty())
        {
            result.errorMessage = "No mesh path specified";
            return result;
        }

        if (request.progressCallback) request.progressCallback(0.0f);

        // Generate atlas layout
        auto atlasData = importTypes::ImposterAtlasGenerator::generateAtlasLayout(request.config);

        if (atlasData.views.empty())
        {
            result.errorMessage = "Failed to generate atlas layout";
            return result;
        }

        result.atlasWidth = atlasData.atlasWidth;
        result.atlasHeight = atlasData.atlasHeight;
        result.viewCount = static_cast<uint32_t>(atlasData.views.size());

        uint32_t viewRes = request.config.viewResolution;

        if (request.progressCallback) request.progressCallback(0.05f);

        // Create a dedicated viewport for baking at view resolution
        RenderTextureViewPort bakeViewport(*devicePtr, *swapChainPtr);
        bakeViewport.init(viewRes, viewRes);
        bakeViewport.setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)); // Transparent background

        if (request.progressCallback) request.progressCallback(0.1f);

        // Ensure atlas colorData is allocated
        size_t atlasPixelCount = static_cast<size_t>(atlasData.atlasWidth) * atlasData.atlasHeight * 4;
        atlasData.colorData.resize(atlasPixelCount, 0);
        if (request.config.generateNormalMap)
        {
            atlasData.normalData.resize(atlasPixelCount, 0);
        }

        // Camera setup: orthographic projection fitting the mesh
        float orthoSize = request.config.meshScale * 1.5f;
        float cameraDistance = request.config.meshScale * 5.0f;
        glm::vec3 meshCenter(0.0f); // Assume mesh is centered at origin
        float nearPlane = 0.01f;
        float farPlane = cameraDistance * 3.0f;

        glm::mat4 projection = glm::ortho(
            -orthoSize, orthoSize,
            -orthoSize, orthoSize,
            nearPlane, farPlane
        );

        // Render each view
        uint32_t totalViews = static_cast<uint32_t>(atlasData.views.size());
        for (uint32_t i = 0; i < totalViews; ++i)
        {
            const auto& viewInfo = atlasData.views[i];

            // Compute camera view matrix from orbital angles
            glm::mat4 view = computeOrbitalView(
                viewInfo.horizontalAngle,
                viewInfo.verticalAngle,
                cameraDistance,
                meshCenter
            );

            glm::vec3 cameraPos = meshCenter + glm::vec3(
                cameraDistance * std::cos(viewInfo.verticalAngle) * std::sin(viewInfo.horizontalAngle),
                cameraDistance * std::sin(viewInfo.verticalAngle),
                cameraDistance * std::cos(viewInfo.verticalAngle) * std::cos(viewInfo.horizontalAngle)
            );

            // Render the scene from this angle
            bakeViewport.render(renderPassHandler, view, projection, cameraPos, nearPlane, farPlane);

            // Wait for GPU to finish
            devicePtr->getLogicalDevice().waitIdle();

            // Read back the rendered pixels
            vk::Image renderedImage = bakeViewport.getLastRenderedImage();
            std::vector<uint8_t> viewPixels;

            if (static_cast<VkImage>(renderedImage) != VK_NULL_HANDLE &&
                readbackImage(static_cast<VkImage>(renderedImage), viewRes, viewRes, viewPixels))
            {
                // Copy this view's pixels into the atlas at the correct position
                uint32_t atlasX = static_cast<uint32_t>(viewInfo.uvRect.x * atlasData.atlasWidth);
                uint32_t atlasY = static_cast<uint32_t>(viewInfo.uvRect.y * atlasData.atlasHeight);
                uint32_t viewW = static_cast<uint32_t>(viewInfo.uvRect.z * atlasData.atlasWidth);
                uint32_t viewH = static_cast<uint32_t>(viewInfo.uvRect.w * atlasData.atlasHeight);

                uint32_t copyW = std::min(viewW, viewRes);
                uint32_t copyH = std::min(viewH, viewRes);

                for (uint32_t row = 0; row < copyH; ++row)
                {
                    size_t srcOffset = static_cast<size_t>(row) * viewRes * 4;
                    size_t dstOffset = (static_cast<size_t>(atlasY + row) * atlasData.atlasWidth + atlasX) * 4;

                    if (srcOffset + copyW * 4 <= viewPixels.size() &&
                        dstOffset + copyW * 4 <= atlasData.colorData.size())
                    {
                        std::memcpy(&atlasData.colorData[dstOffset], &viewPixels[srcOffset], copyW * 4);
                    }
                }
            }
            else
            {
                vfLogWarning("ImposterBaker: Failed to read back view {} pixels", i);
            }

            if (request.progressCallback)
            {
                float progress = 0.1f + 0.8f * (static_cast<float>(i + 1) / static_cast<float>(totalViews));
                request.progressCallback(progress);
            }
        }

        if (request.progressCallback) request.progressCallback(0.9f);

        // Cleanup bake viewport
        bakeViewport.cleanUp();

        // Save to .vfImposter
        std::string outputPath = request.outputPath;
        if (outputPath.empty())
        {
            outputPath = request.meshPath;
            auto dotPos = outputPath.rfind('.');
            if (dotPos != std::string::npos)
            {
                outputPath = outputPath.substr(0, dotPos);
            }
            outputPath += ".vfImposter";
        }

        if (importTypes::ImposterSerializer::save(outputPath, atlasData))
        {
            result.success = true;
            result.outputPath = outputPath;
            vfLogInfo("ImposterBaker: Saved atlas to {}", outputPath);
        }
        else
        {
            result.errorMessage = "Failed to save .vfImposter file";
        }

        if (request.progressCallback) request.progressCallback(1.0f);

        return result;
    }
}
