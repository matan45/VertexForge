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
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <array>

namespace render::impostor
{
    // .vfImposter binary format constants
    static constexpr std::array<char, 4> IMPOSTER_MAGIC = { 'V', 'F', 'I', 'M' };
    static constexpr uint32_t IMPOSTER_FORMAT_VERSION = 1;

    void ImposterBaker::init(core::Device& device, core::SwapChain& swapChain)
    {
        devicePtr = &device;
        swapChainPtr = &swapChain;
    }

    ImposterBaker::AtlasLayout ImposterBaker::generateAtlasLayout(const BakeAtlasConfig& config)
    {
        AtlasLayout layout;

        uint32_t totalViews = config.horizontalAngles * config.verticalAngles;
        uint32_t cols = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(totalViews))));
        uint32_t rows = static_cast<uint32_t>(std::ceil(static_cast<double>(totalViews) / cols));

        layout.atlasWidth = cols * config.viewResolution;
        layout.atlasHeight = rows * config.viewResolution;

        float invAtlasW = 1.0f / static_cast<float>(layout.atlasWidth);
        float invAtlasH = 1.0f / static_cast<float>(layout.atlasHeight);
        float viewUvW = static_cast<float>(config.viewResolution) * invAtlasW;
        float viewUvH = static_cast<float>(config.viewResolution) * invAtlasH;

        float vertStep = (config.verticalAngles > 1)
            ? (glm::pi<float>() / 3.0f) / static_cast<float>(config.verticalAngles - 1)
            : 0.0f;
        float horizStep = glm::two_pi<float>() / static_cast<float>(config.horizontalAngles);

        layout.views.reserve(totalViews);

        for (uint32_t v = 0; v < config.verticalAngles; ++v)
        {
            float vertAngle = static_cast<float>(v) * vertStep;

            for (uint32_t h = 0; h < config.horizontalAngles; ++h)
            {
                float horizAngle = static_cast<float>(h) * horizStep;

                uint32_t viewIndex = v * config.horizontalAngles + h;
                uint32_t col = viewIndex % cols;
                uint32_t row = viewIndex / cols;

                float uvX = static_cast<float>(col * config.viewResolution) * invAtlasW;
                float uvY = static_cast<float>(row * config.viewResolution) * invAtlasH;

                ViewInfo view;
                view.horizontalAngle = horizAngle;
                view.verticalAngle = vertAngle;
                view.uvRect = glm::vec4(uvX, uvY, viewUvW, viewUvH);

                layout.views.push_back(view);
            }
        }

        return layout;
    }

    bool ImposterBaker::saveAtlas(const std::string& filePath, const AtlasLayout& layout,
                                   const BakeAtlasConfig& config,
                                   const std::vector<uint8_t>& colorData,
                                   const std::vector<uint8_t>& normalData)
    {
        if (layout.atlasWidth == 0 || layout.atlasHeight == 0 || layout.views.empty())
        {
            vfLogError("ImposterBaker: Cannot save empty atlas data");
            return false;
        }

        namespace fs = std::filesystem;
        fs::path path(filePath);
        if (path.has_parent_path())
        {
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            if (ec)
            {
                vfLogError("ImposterBaker: Failed to create directory {}: {}",
                           path.parent_path().string(), ec.message());
                return false;
            }
        }

        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("ImposterBaker: Failed to open file for writing: {}", filePath);
            return false;
        }

        // Header
        file.write(IMPOSTER_MAGIC.data(), 4);
        uint32_t version = IMPOSTER_FORMAT_VERSION;
        file.write(reinterpret_cast<const char*>(&version), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&layout.atlasWidth), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&layout.atlasHeight), sizeof(uint32_t));

        uint32_t viewCount = static_cast<uint32_t>(layout.views.size());
        file.write(reinterpret_cast<const char*>(&viewCount), sizeof(uint32_t));

        uint8_t hasNormalMap = config.generateNormalMap ? 1 : 0;
        file.write(reinterpret_cast<const char*>(&hasNormalMap), sizeof(uint8_t));

        file.write(reinterpret_cast<const char*>(&config.horizontalAngles), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&config.verticalAngles), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&config.viewResolution), sizeof(uint32_t));

        // View infos
        for (const auto& view : layout.views)
        {
            file.write(reinterpret_cast<const char*>(&view.horizontalAngle), sizeof(float));
            file.write(reinterpret_cast<const char*>(&view.verticalAngle), sizeof(float));
            file.write(reinterpret_cast<const char*>(&view.uvRect.x), sizeof(float));
            file.write(reinterpret_cast<const char*>(&view.uvRect.y), sizeof(float));
            file.write(reinterpret_cast<const char*>(&view.uvRect.z), sizeof(float));
            file.write(reinterpret_cast<const char*>(&view.uvRect.w), sizeof(float));
        }

        // Color data
        size_t pixelDataSize = static_cast<size_t>(layout.atlasWidth) * layout.atlasHeight * 4;
        if (colorData.size() != pixelDataSize)
        {
            vfLogError("ImposterBaker: Color data size mismatch. Expected {}, got {}",
                       pixelDataSize, colorData.size());
            return false;
        }
        file.write(reinterpret_cast<const char*>(colorData.data()),
                   static_cast<std::streamsize>(pixelDataSize));

        // Normal data
        if (hasNormalMap)
        {
            if (normalData.size() != pixelDataSize)
            {
                vfLogError("ImposterBaker: Normal data size mismatch. Expected {}, got {}",
                           pixelDataSize, normalData.size());
                return false;
            }
            file.write(reinterpret_cast<const char*>(normalData.data()),
                       static_cast<std::streamsize>(pixelDataSize));
        }

        if (!file.good())
        {
            vfLogError("ImposterBaker: Write error saving atlas to {}", filePath);
            return false;
        }

        return true;
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

        // Record commands: transition image -> copy to buffer -> transition back
        auto cmd = core::Utilities::beginSingleTimeCommands(vkDevice, cmdPool);

        core::ImageUtilities::transitionImageLayout(cmd.get(), srcImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageAspectFlagBits::eColor);

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

        core::ImageUtilities::transitionImageLayout(cmd.get(), srcImage,
            vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::Utilities::endSingleTimeCommands(queue, cmd);

        // Map and copy
        void* mapped = vkDevice.mapMemory(stagingMemory, 0, imageSize);
        std::memcpy(outPixels.data(), mapped, imageSize);
        vkDevice.unmapMemory(stagingMemory);

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
        auto layout = generateAtlasLayout(request.config);

        if (layout.views.empty())
        {
            result.errorMessage = "Failed to generate atlas layout";
            return result;
        }

        result.atlasWidth = layout.atlasWidth;
        result.atlasHeight = layout.atlasHeight;
        result.viewCount = static_cast<uint32_t>(layout.views.size());

        uint32_t viewRes = request.config.viewResolution;

        if (request.progressCallback) request.progressCallback(0.05f);

        // Create a dedicated viewport for baking at view resolution
        RenderTextureViewPort bakeViewport(*devicePtr, *swapChainPtr);
        bakeViewport.init(viewRes, viewRes);
        bakeViewport.setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));

        if (request.progressCallback) request.progressCallback(0.1f);

        // Allocate atlas pixel buffers
        size_t atlasPixelCount = static_cast<size_t>(layout.atlasWidth) * layout.atlasHeight * 4;
        std::vector<uint8_t> colorData(atlasPixelCount, 0);
        std::vector<uint8_t> normalData;
        if (request.config.generateNormalMap)
        {
            normalData.resize(atlasPixelCount, 0);
        }

        // Camera setup
        float orthoSize = request.config.meshScale * 1.5f;
        float cameraDistance = request.config.meshScale * 5.0f;
        glm::vec3 meshCenter(0.0f);
        float nearPlane = 0.01f;
        float farPlane = cameraDistance * 3.0f;

        glm::mat4 projection = glm::ortho(
            -orthoSize, orthoSize,
            -orthoSize, orthoSize,
            nearPlane, farPlane
        );

        // Render each view
        uint32_t totalViews = static_cast<uint32_t>(layout.views.size());
        uint32_t cols = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(totalViews))));

        for (uint32_t i = 0; i < totalViews; ++i)
        {
            const auto& viewInfo = layout.views[i];

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

            bakeViewport.render(renderPassHandler, view, projection, cameraPos, nearPlane, farPlane);
            devicePtr->getLogicalDevice().waitIdle();

            vk::Image renderedImage = bakeViewport.getLastRenderedImage();
            std::vector<uint8_t> viewPixels;

            if (static_cast<VkImage>(renderedImage) != VK_NULL_HANDLE &&
                readbackImage(static_cast<VkImage>(renderedImage), viewRes, viewRes, viewPixels))
            {
                // Copy view pixels into atlas at correct position
                uint32_t atlasX = static_cast<uint32_t>(viewInfo.uvRect.x * layout.atlasWidth);
                uint32_t atlasY = static_cast<uint32_t>(viewInfo.uvRect.y * layout.atlasHeight);
                uint32_t viewW = static_cast<uint32_t>(viewInfo.uvRect.z * layout.atlasWidth);
                uint32_t viewH = static_cast<uint32_t>(viewInfo.uvRect.w * layout.atlasHeight);

                uint32_t copyW = std::min(viewW, viewRes);
                uint32_t copyH = std::min(viewH, viewRes);

                for (uint32_t row = 0; row < copyH; ++row)
                {
                    size_t srcOffset = static_cast<size_t>(row) * viewRes * 4;
                    size_t dstOffset = (static_cast<size_t>(atlasY + row) * layout.atlasWidth + atlasX) * 4;

                    if (srcOffset + copyW * 4 <= viewPixels.size() &&
                        dstOffset + copyW * 4 <= colorData.size())
                    {
                        std::memcpy(&colorData[dstOffset], &viewPixels[srcOffset], copyW * 4);
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

        bakeViewport.cleanUp();

        // Determine output path
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

        // Save atlas
        if (saveAtlas(outputPath, layout, request.config, colorData, normalData))
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
