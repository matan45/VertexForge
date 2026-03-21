#include "ColorGradingEffect.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/ImageUtilities.hpp"
#include "../../../core/MemoryUtilities.hpp"
#include "../../../core/Utilities.hpp"
#include <spdlog/spdlog.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace
{
    // Convert float32 to float16 (IEEE 754 half-precision)
    uint16_t floatToHalf(float value)
    {
        uint32_t f;
        std::memcpy(&f, &value, sizeof(f));
        uint32_t sign = (f >> 16) & 0x8000;
        int32_t exponent = ((f >> 23) & 0xFF) - 127 + 15;
        uint32_t mantissa = f & 0x7FFFFF;

        if (exponent <= 0)
            return static_cast<uint16_t>(sign);
        if (exponent >= 31)
            return static_cast<uint16_t>(sign | 0x7C00);

        return static_cast<uint16_t>(sign | (exponent << 10) | (mantissa >> 13));
    }
}

namespace render::postprocess
{
    ColorGradingEffect::ColorGradingEffect(core::Device& device)
        : device{device}
    {
        enabled = false;
    }

    void ColorGradingEffect::init(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        loadShader();
        createDescriptorSetLayouts();
        createPipeline(renderPass, extent);
        createLUTSampler();
        createUBO();
        generateIdentityLUT(32);
        createDescriptorPool();
        allocateDescriptorSet();
        updateDescriptorSets();
        initialized = true;
    }

    void ColorGradingEffect::cleanup()
    {
        auto& dev = device.getLogicalDevice();

        if (graphicsPipeline)
        {
            dev.destroyPipeline(graphicsPipeline);
            graphicsPipeline = nullptr;
        }

        if (pipelineLayout)
        {
            dev.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (lutDescriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(lutDescriptorSetLayout);
            lutDescriptorSetLayout = nullptr;
        }

        if (inputDescriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(inputDescriptorSetLayout);
            inputDescriptorSetLayout = nullptr;
        }

        if (paramsBuffer)
        {
            if (paramsBufferMapped)
            {
                dev.unmapMemory(paramsBufferMemory);
                paramsBufferMapped = nullptr;
            }
            core::BufferUtilities::destroyBuffer(dev, paramsBuffer, paramsBufferMemory);
        }

        if (lutSampler)
        {
            dev.destroySampler(lutSampler);
            lutSampler = nullptr;
        }

        destroyLUT(primaryLut);
        destroyLUT(secondaryLut);
        destroyLUT(identityLut);

        if (shader)
        {
            shader->cleanUp();
            shader.reset();
        }

        initialized = false;
    }

    void ColorGradingEffect::recreate(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        auto& dev = device.getLogicalDevice();

        if (graphicsPipeline)
        {
            dev.destroyPipeline(graphicsPipeline);
            graphicsPipeline = nullptr;
        }

        if (pipelineLayout)
        {
            dev.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        createPipeline(renderPass, extent);
    }

    void ColorGradingEffect::record(const vk::CommandBuffer& commandBuffer,
                                     vk::DescriptorSet inputDescriptorSet)
    {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        std::array<vk::DescriptorSet, 2> sets = {inputDescriptorSet, lutDescriptorSet};
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                          0, static_cast<uint32_t>(sets.size()),
                                          sets.data(), 0, nullptr);

        commandBuffer.draw(3, 1, 0, 0);
    }

    void ColorGradingEffect::preRecord(const vk::CommandBuffer& commandBuffer,
                                        vk::DescriptorSet inputDescriptorSet)
    {
        // Deferred LUT reload - safe to destroy/create since previous frame is complete
        if (primaryPathPending)
        {
            destroyLUT(primaryLut);
            currentPrimaryPath = pendingPrimaryPath;
            if (!currentPrimaryPath.empty())
            {
                loadLUT(currentPrimaryPath, primaryLut);
            }
            primaryPathPending = false;
            descriptorsDirty = true;
        }

        if (secondaryPathPending)
        {
            destroyLUT(secondaryLut);
            currentSecondaryPath = pendingSecondaryPath;
            if (!currentSecondaryPath.empty())
            {
                loadLUT(currentSecondaryPath, secondaryLut);
            }
            secondaryPathPending = false;
            descriptorsDirty = true;
        }

        if (descriptorsDirty && initialized)
        {
            updateDescriptorSets();
            descriptorsDirty = false;
        }
    }

    void ColorGradingEffect::updateParameters(const ::postprocess::PostProcessSettings& settings)
    {
        const auto& cg = settings.colorGrading;
        enabled = cg.enabled;

        // Flag LUT path changes for deferred reload in preRecord
        if (currentPrimaryPath != cg.primaryLutPath && !primaryPathPending)
        {
            pendingPrimaryPath = cg.primaryLutPath;
            primaryPathPending = true;
        }

        if (currentSecondaryPath != cg.secondaryLutPath && !secondaryPathPending)
        {
            pendingSecondaryPath = cg.secondaryLutPath;
            secondaryPathPending = true;
        }

        // Update UBO
        if (paramsBufferMapped)
        {
            // Determine which LUT size to use (primary > identity)
            uint32_t activeLutSize = primaryLut.size > 0 ? primaryLut.size : identityLut.size;

            ColorGradingParamsUBO ubo{};
            ubo.lift[0] = cg.liftR; ubo.lift[1] = cg.liftG; ubo.lift[2] = cg.liftB; ubo.lift[3] = 0.0f;
            ubo.gamma[0] = cg.gammaR; ubo.gamma[1] = cg.gammaG; ubo.gamma[2] = cg.gammaB; ubo.gamma[3] = 0.0f;
            ubo.gain[0] = cg.gainR; ubo.gain[1] = cg.gainG; ubo.gain[2] = cg.gainB; ubo.gain[3] = 0.0f;
            ubo.saturation = cg.saturation;
            ubo.colorTemperature = cg.colorTemperature;
            ubo.colorTint = cg.colorTint;
            ubo.lutIntensity = cg.lutIntensity;
            ubo.lutBlendFactor = cg.lutBlendFactor;
            ubo.hasSecondaryLut = secondaryLut.size > 0 ? 1.0f : 0.0f;
            ubo.lutSize = static_cast<float>(activeLutSize);
            ubo.pad = 0.0f;

            std::memcpy(paramsBufferMapped, &ubo, sizeof(ColorGradingParamsUBO));
        }
    }

    // ---- Resource creation ----

    void ColorGradingEffect::loadShader()
    {
        shader = std::make_shared<core::Shader>(device);
        shader->readShader("../../resources/shaders/postprocess/color_grading.glsl");
    }

    void ColorGradingEffect::createDescriptorSetLayouts()
    {
        auto& dev = device.getLogicalDevice();

        // Set 0: input color sampler (same as VignetteEffect)
        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;

            inputDescriptorSetLayout = dev.createDescriptorSetLayout(layoutInfo);
        }

        // Set 1: primary LUT, secondary LUT, params UBO
        {
            std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};

            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[2].binding = 2;
            bindings[2].descriptorType = vk::DescriptorType::eUniformBuffer;
            bindings[2].descriptorCount = 1;
            bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;

            lutDescriptorSetLayout = core::PipelineUtilities::createUpdateAfterBindLayout(dev, bindings.data(), static_cast<uint32_t>(bindings.size()));
        }
    }

    void ColorGradingEffect::createPipeline(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        core::GraphicsPipelineConfig config{};
        config.device = device.getLogicalDevice();
        config.renderPass = renderPass;
        config.extent = extent;
        config.shaderStages = shader->getShaderStages();
        config.descriptorSetLayouts = {inputDescriptorSetLayout, lutDescriptorSetLayout};
        config.depthTestEnable = false;
        config.depthWriteEnable = false;
        config.blendEnable = false;
        config.cullMode = vk::CullModeFlagBits::eNone;

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
    }

    void ColorGradingEffect::createLUTSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        lutSampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void ColorGradingEffect::createUBO()
    {
        auto& dev = device.getLogicalDevice();

        core::BufferInfoRequest bufReq(dev, device.getPhysicalDevice());
        bufReq.size = sizeof(ColorGradingParamsUBO);
        bufReq.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufReq.properties = vk::MemoryPropertyFlagBits::eHostVisible
                          | vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(bufReq, paramsBuffer, paramsBufferMemory);
        paramsBufferMapped = dev.mapMemory(paramsBufferMemory, 0, sizeof(ColorGradingParamsUBO));
    }

    void ColorGradingEffect::createDescriptorPool()
    {
        std::array<vk::DescriptorPoolSize, 2> poolSizes{};

        // 2 combined image samplers (primary + secondary LUT)
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = 2;

        // 1 UBO
        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 1;

        descriptorPool = core::PipelineUtilities::createUpdateAfterBindPool(device.getLogicalDevice(), 1, poolSizes.data(), static_cast<uint32_t>(poolSizes.size()), vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
    }

    void ColorGradingEffect::allocateDescriptorSet()
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &lutDescriptorSetLayout;

        lutDescriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
    }

    void ColorGradingEffect::updateDescriptorSets()
    {
        auto& dev = device.getLogicalDevice();

        // Use primary LUT if loaded, otherwise identity
        vk::ImageView primaryView = primaryLut.size > 0 ? primaryLut.imageView : identityLut.imageView;
        // Use secondary LUT if loaded, otherwise identity (won't be sampled if hasSecondaryLut = 0)
        vk::ImageView secondaryView = secondaryLut.size > 0 ? secondaryLut.imageView : identityLut.imageView;

        vk::DescriptorImageInfo primaryInfo{};
        primaryInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        primaryInfo.imageView = primaryView;
        primaryInfo.sampler = lutSampler;

        vk::DescriptorImageInfo secondaryInfo{};
        secondaryInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        secondaryInfo.imageView = secondaryView;
        secondaryInfo.sampler = lutSampler;

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = paramsBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(ColorGradingParamsUBO);

        std::array<vk::WriteDescriptorSet, 3> writes{};

        writes[0].dstSet = lutDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &primaryInfo;

        writes[1].dstSet = lutDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &secondaryInfo;

        writes[2].dstSet = lutDescriptorSet;
        writes[2].dstBinding = 2;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &bufferInfo;

        dev.updateDescriptorSets(writes, nullptr);
    }

    // ---- LUT management ----

    void ColorGradingEffect::generateIdentityLUT(uint32_t size)
    {
        std::vector<uint16_t> data(size * size * size * 4);

        for (uint32_t z = 0; z < size; z++)
        {
            for (uint32_t y = 0; y < size; y++)
            {
                for (uint32_t x = 0; x < size; x++)
                {
                    uint32_t idx = (z * size * size + y * size + x) * 4;
                    data[idx + 0] = floatToHalf(static_cast<float>(x) / static_cast<float>(size - 1));
                    data[idx + 1] = floatToHalf(static_cast<float>(y) / static_cast<float>(size - 1));
                    data[idx + 2] = floatToHalf(static_cast<float>(z) / static_cast<float>(size - 1));
                    data[idx + 3] = floatToHalf(1.0f);
                }
            }
        }

        create3DImage(size, identityLut);
        upload3DImageData(identityLut, data.data(), data.size() * sizeof(uint16_t));
    }

    void ColorGradingEffect::create3DImage(uint32_t size, LUTTexture& lut)
    {
        auto& dev = device.getLogicalDevice();
        auto& physDev = device.getPhysicalDevice();

        vk::ImageCreateInfo imageInfo{};
        imageInfo.imageType = vk::ImageType::e3D;
        imageInfo.extent = vk::Extent3D{size, size, size};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = vk::Format::eR16G16B16A16Sfloat;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;
        imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;

        lut.image = dev.createImage(imageInfo);

        vk::MemoryRequirements memReqs = dev.getImageMemoryRequirements(lut.image);
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = core::MemoryUtilities::findMemoryType(
            physDev, memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

        lut.memory = dev.allocateMemory(allocInfo);
        dev.bindImageMemory(lut.image, lut.memory, 0);

        core::ImageViewInfoRequest viewReq(dev, lut.image);
        viewReq.format = vk::Format::eR16G16B16A16Sfloat;
        viewReq.imageType = vk::ImageViewType::e3D;
        viewReq.aspectFlags = vk::ImageAspectFlagBits::eColor;
        core::ImageUtilities::createImageView(viewReq, lut.imageView);

        lut.size = size;
    }

    void ColorGradingEffect::upload3DImageData(LUTTexture& lut, const void* data, size_t dataSize)
    {
        auto vkDevice = device.getLogicalDevice();

        // Create staging buffer
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;
        core::BufferInfoRequest stagingReq(vkDevice, device.getPhysicalDevice());
        stagingReq.size = dataSize;
        stagingReq.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingReq.properties = vk::MemoryPropertyFlagBits::eHostVisible
                              | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(stagingReq, stagingBuffer, stagingMemory);

        void* mapped = vkDevice.mapMemory(stagingMemory, 0, dataSize);
        std::memcpy(mapped, data, dataSize);
        vkDevice.unmapMemory(stagingMemory);

        auto cmd = core::Utilities::beginSingleTimeCommands(vkDevice, device.getStagingCommandPool());

        core::ImageUtilities::transitionImageLayout(
            cmd.get(), lut.image,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageAspectFlagBits::eColor);

        vk::BufferImageCopy region{};
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = vk::Extent3D{lut.size, lut.size, lut.size};

        cmd->copyBufferToImage(stagingBuffer, lut.image,
                               vk::ImageLayout::eTransferDstOptimal, region);

        core::ImageUtilities::transitionImageLayout(
            cmd.get(), lut.image,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::Utilities::endSingleTimeCommands(device, cmd);

        vkDevice.destroyBuffer(stagingBuffer);
        vkDevice.freeMemory(stagingMemory);
    }

    void ColorGradingEffect::loadLUT(const std::string& path, LUTTexture& lut)
    {
        loadLUTFromCube(path, lut);
    }

    void ColorGradingEffect::loadLUTFromCube(const std::string& path, LUTTexture& lut)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            spdlog::error("ColorGrading: Failed to open .cube file: {}", path);
            return;
        }

        uint32_t lutSize = 0;
        std::vector<float> rgbData;
        std::string line;

        while (std::getline(file, line))
        {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#')
                continue;

            // Check for LUT_3D_SIZE
            if (line.rfind("LUT_3D_SIZE", 0) == 0)
            {
                std::istringstream iss(line.substr(11));
                iss >> lutSize;
                rgbData.reserve(lutSize * lutSize * lutSize * 3);
                continue;
            }

            // Skip other keywords
            if (line.rfind("TITLE", 0) == 0 || line.rfind("DOMAIN_MIN", 0) == 0 ||
                line.rfind("DOMAIN_MAX", 0) == 0)
                continue;

            // Parse RGB triplet
            if (lutSize > 0)
            {
                float r, g, b;
                std::istringstream iss(line);
                if (iss >> r >> g >> b)
                {
                    rgbData.push_back(r);
                    rgbData.push_back(g);
                    rgbData.push_back(b);
                }
            }
        }

        if (lutSize == 0 || rgbData.size() != lutSize * lutSize * lutSize * 3)
        {
            spdlog::error("ColorGrading: Invalid .cube file: {} (size={}, entries={})",
                          path, lutSize, rgbData.size() / 3);
            return;
        }

        // Convert to RGBA16F (half-precision float)
        std::vector<uint16_t> data(lutSize * lutSize * lutSize * 4);
        for (size_t i = 0; i < lutSize * lutSize * lutSize; i++)
        {
            data[i * 4 + 0] = floatToHalf(std::clamp(rgbData[i * 3 + 0], 0.0f, 1.0f));
            data[i * 4 + 1] = floatToHalf(std::clamp(rgbData[i * 3 + 1], 0.0f, 1.0f));
            data[i * 4 + 2] = floatToHalf(std::clamp(rgbData[i * 3 + 2], 0.0f, 1.0f));
            data[i * 4 + 3] = floatToHalf(1.0f);
        }

        create3DImage(lutSize, lut);
        upload3DImageData(lut, data.data(), data.size() * sizeof(uint16_t));
    }

    void ColorGradingEffect::destroyLUT(LUTTexture& lut)
    {
        if (lut.size == 0)
            return;

        auto& dev = device.getLogicalDevice();

        if (lut.imageView)
        {
            dev.destroyImageView(lut.imageView);
            lut.imageView = nullptr;
        }
        if (lut.image)
        {
            dev.destroyImage(lut.image);
            lut.image = nullptr;
        }
        if (lut.memory)
        {
            dev.freeMemory(lut.memory);
            lut.memory = nullptr;
        }
        lut.size = 0;
    }
}
