#include "HiZBuffer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Utilities.hpp"
#include "resource/ShaderResource.hpp"
#include "print/Logger.hpp"
#include <shaderc/shaderc.hpp>
#include <algorithm>
#include <cmath>

namespace render::occlusion {

    HiZBuffer::HiZBuffer(core::Device& device, core::SwapChain& swapChain)
        : device(device), swapChain(swapChain) {}

    HiZBuffer::~HiZBuffer() {
        cleanup();
    }

    void HiZBuffer::init(vk::Image depthImage, vk::ImageView depthImageView, vk::Format format) {
        sourceDepthImage = depthImage;
        sourceDepthView = depthImageView;
        depthFormat = format;
        width = swapChain.getSwapchainExtent().width;
        height = swapChain.getSwapchainExtent().height;

        // Calculate mip levels
        mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

        createHiZImage();
        createHiZSampler();
        createComputePipeline();
        createDescriptorSets();

        initialized = true;
        loggerInfo("Hi-Z buffer initialized: {}x{} with {} mip levels", width, height, mipLevels);
    }

    void HiZBuffer::createHiZImage() {
        // Create Hi-Z image with mipmap chain
        vk::ImageCreateInfo imageInfo{};
        imageInfo.imageType = vk::ImageType::e2D;
        imageInfo.format = vk::Format::eR32Sfloat;  // Single channel float for depth
        imageInfo.extent = vk::Extent3D(width, height, 1);
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.usage = vk::ImageUsageFlagBits::eSampled |
                          vk::ImageUsageFlagBits::eStorage |
                          vk::ImageUsageFlagBits::eTransferDst;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;

        hiZImage = device.getLogicalDevice().createImage(imageInfo);

        // Allocate memory
        vk::MemoryRequirements memReqs = device.getLogicalDevice().getImageMemoryRequirements(hiZImage);
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = core::Utilities::findMemoryType(
            device.getPhysicalDevice(),
            memReqs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );

        hiZMemory = device.getLogicalDevice().allocateMemory(allocInfo);
        device.getLogicalDevice().bindImageMemory(hiZImage, hiZMemory, 0);

        // Create full mip chain view
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = hiZImage;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = vk::Format::eR32Sfloat;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        hiZImageView = device.getLogicalDevice().createImageView(viewInfo);

        // Create per-mip views for compute shader
        mipViews.resize(mipLevels);
        for (uint32_t i = 0; i < mipLevels; ++i) {
            vk::ImageViewCreateInfo mipViewInfo = viewInfo;
            mipViewInfo.subresourceRange.baseMipLevel = i;
            mipViewInfo.subresourceRange.levelCount = 1;
            mipViews[i] = device.getLogicalDevice().createImageView(mipViewInfo);
        }
    }

    void HiZBuffer::createHiZSampler() {
        // Sampler for reading Hi-Z in shaders
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eNearest;  // Point sampling for Hi-Z
        samplerInfo.minFilter = vk::Filter::eNearest;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);

        hiZSampler = device.getLogicalDevice().createSampler(samplerInfo);

        // Sampler for reading source depth
        vk::SamplerCreateInfo depthSamplerInfo = samplerInfo;
        depthSamplerInfo.maxLod = 0.0f;
        depthSampler = device.getLogicalDevice().createSampler(depthSamplerInfo);
    }

    void HiZBuffer::createComputePipeline() {
        // Load and compile compute shader
        auto shaders = resource::ShaderResource::readShaderFile("../../resources/shaders/hiz/hiz_generate.glsl");
        if (shaders.empty()) {
            loggerError("Failed to load Hi-Z compute shader source");
            return;
        }

        // Find the compute shader source
        std::string computeSource;
        for (const auto& shader : shaders) {
            if (shader.type == resource::ShaderType::COMPUTE) {
                computeSource = shader.source;
                break;
            }
        }

        if (computeSource.empty()) {
            loggerError("No compute shader found in hiz_generate.comp");
            return;
        }

        // Compile GLSL to SPIR-V using shaderc
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
        options.SetOptimizationLevel(shaderc_optimization_level_performance);

        shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
            computeSource.data(), computeSource.size(),
            shaderc_compute_shader, "hiz_generate.comp", options);

        if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
            loggerError("Hi-Z shader compilation failed: {}", result.GetErrorMessage());
            return;
        }

        std::vector<uint32_t> spirvCode(result.cbegin(), result.cend());

        vk::ShaderModuleCreateInfo shaderInfo{};
        shaderInfo.codeSize = spirvCode.size() * sizeof(uint32_t);
        shaderInfo.pCode = spirvCode.data();
        vk::ShaderModule shaderModule = device.getLogicalDevice().createShaderModule(shaderInfo);

        // Descriptor set layout
        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};

        // Input depth/mip texture
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Output mip texture
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageImage;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);

        // Push constants for output size, input size, and first mip flag
        vk::PushConstantRange pushConstant{};
        pushConstant.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushConstant.offset = 0;
        pushConstant.size = sizeof(int32_t) * 6;  // outputSize(2) + inputSize(2) + isFirstMip + padding

        // Pipeline layout
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
        pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);

        // Compute pipeline
        vk::PipelineShaderStageCreateInfo stageInfo{};
        stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = pipelineLayout;

        auto pipelineResult = device.getLogicalDevice().createComputePipeline(nullptr, pipelineInfo);
        computePipeline = pipelineResult.value;

        device.getLogicalDevice().destroyShaderModule(shaderModule);
    }

    void HiZBuffer::createDescriptorSets() {
        // Create descriptor pool
        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = mipLevels;
        poolSizes[1].type = vk::DescriptorType::eStorageImage;
        poolSizes[1].descriptorCount = mipLevels;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = mipLevels;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);

        // Allocate descriptor sets (one per mip transition)
        std::vector<vk::DescriptorSetLayout> layouts(mipLevels, descriptorSetLayout);
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = mipLevels;
        allocInfo.pSetLayouts = layouts.data();
        descriptorSets = device.getLogicalDevice().allocateDescriptorSets(allocInfo);

        // Update descriptor sets
        for (uint32_t i = 0; i < mipLevels; ++i) {
            vk::DescriptorImageInfo inputInfo{};
            inputInfo.sampler = (i == 0) ? depthSampler : hiZSampler;
            inputInfo.imageView = (i == 0) ? sourceDepthView : mipViews[i - 1];
            inputInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            vk::DescriptorImageInfo outputInfo{};
            outputInfo.imageView = mipViews[i];
            outputInfo.imageLayout = vk::ImageLayout::eGeneral;

            std::array<vk::WriteDescriptorSet, 2> writes{};

            writes[0].dstSet = descriptorSets[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[0].pImageInfo = &inputInfo;

            writes[1].dstSet = descriptorSets[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = vk::DescriptorType::eStorageImage;
            writes[1].pImageInfo = &outputInfo;

            device.getLogicalDevice().updateDescriptorSets(writes, {});
        }
    }

    void HiZBuffer::generate(vk::CommandBuffer cmd) {
        if (!initialized) return;

        // Transition source depth buffer to shader read layout
        // Note: Format may have both depth and stencil, so include both aspects
        {
            vk::ImageMemoryBarrier depthBarrier{};
            depthBarrier.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            depthBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depthBarrier.image = sourceDepthImage;
            depthBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
            depthBarrier.subresourceRange.baseMipLevel = 0;
            depthBarrier.subresourceRange.levelCount = 1;
            depthBarrier.subresourceRange.baseArrayLayer = 0;
            depthBarrier.subresourceRange.layerCount = 1;
            depthBarrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            depthBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eLateFragmentTests,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, {}, depthBarrier
            );
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);

        uint32_t mipWidth = width;
        uint32_t mipHeight = height;

        for (uint32_t i = 0; i < mipLevels; ++i) {
            uint32_t outputWidth = std::max(1u, mipWidth / 2);
            uint32_t outputHeight = std::max(1u, mipHeight / 2);

            if (i == 0) {
                outputWidth = mipWidth;
                outputHeight = mipHeight;
            }

            // Transition output mip to general for writing
            vk::ImageMemoryBarrier barrier{};
            barrier.oldLayout = vk::ImageLayout::eUndefined;
            barrier.newLayout = vk::ImageLayout::eGeneral;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = hiZImage;
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            barrier.subresourceRange.baseMipLevel = i;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = vk::AccessFlagBits::eNone;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, {}, barrier
            );

            // Bind descriptor set and push constants
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout,
                                   0, descriptorSets[i], {});

            // Push constants: outputSize, inputSize, isFirstMip, padding
            struct HiZPushConstants {
                int32_t outputWidth;
                int32_t outputHeight;
                int32_t inputWidth;
                int32_t inputHeight;
                int32_t isFirstMip;
                int32_t padding;
            } pushData;
            pushData.outputWidth = static_cast<int32_t>(outputWidth);
            pushData.outputHeight = static_cast<int32_t>(outputHeight);
            pushData.inputWidth = static_cast<int32_t>(mipWidth);
            pushData.inputHeight = static_cast<int32_t>(mipHeight);
            pushData.isFirstMip = (i == 0) ? 1 : 0;
            pushData.padding = 0;

            cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute,
                             0, sizeof(pushData), &pushData);

            // Dispatch compute shader
            uint32_t groupsX = (outputWidth + 7) / 8;
            uint32_t groupsY = (outputHeight + 7) / 8;
            cmd.dispatch(groupsX, groupsY, 1);

            // Transition output mip to shader read for next iteration
            barrier.oldLayout = vk::ImageLayout::eGeneral;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, {}, barrier
            );

            mipWidth = outputWidth;
            mipHeight = outputHeight;
        }

        // Transition depth buffer back to depth attachment layout for next frame's rendering
        {
            vk::ImageMemoryBarrier depthBarrier{};
            depthBarrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            depthBarrier.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depthBarrier.image = sourceDepthImage;
            depthBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
            depthBarrier.subresourceRange.baseMipLevel = 0;
            depthBarrier.subresourceRange.levelCount = 1;
            depthBarrier.subresourceRange.baseArrayLayer = 0;
            depthBarrier.subresourceRange.layerCount = 1;
            depthBarrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
            depthBarrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                                         vk::AccessFlagBits::eDepthStencilAttachmentWrite;

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eEarlyFragmentTests,
                {}, {}, {}, depthBarrier
            );
        }
    }

    bool HiZBuffer::testAABBVisible(const math::AABB& worldAABB,
                                     const glm::mat4& viewProj,
                                     float nearPlane) const {
        if (!initialized) return true;  // Assume visible if not initialized

        // Project AABB corners to screen space
        glm::vec3 corners[8] = {
            {worldAABB.min.x, worldAABB.min.y, worldAABB.min.z},
            {worldAABB.max.x, worldAABB.min.y, worldAABB.min.z},
            {worldAABB.min.x, worldAABB.max.y, worldAABB.min.z},
            {worldAABB.max.x, worldAABB.max.y, worldAABB.min.z},
            {worldAABB.min.x, worldAABB.min.y, worldAABB.max.z},
            {worldAABB.max.x, worldAABB.min.y, worldAABB.max.z},
            {worldAABB.min.x, worldAABB.max.y, worldAABB.max.z},
            {worldAABB.max.x, worldAABB.max.y, worldAABB.max.z}
        };

        glm::vec2 minScreen(FLT_MAX);
        glm::vec2 maxScreen(-FLT_MAX);
        float minDepth = FLT_MAX;

        bool allBehindCamera = true;

        for (const auto& corner : corners) {
            glm::vec4 clip = viewProj * glm::vec4(corner, 1.0f);

            // Check if behind near plane
            if (clip.w > nearPlane) {
                allBehindCamera = false;

                // Perspective divide
                glm::vec3 ndc = glm::vec3(clip) / clip.w;

                // NDC to screen space [0, 1]
                glm::vec2 screen = (glm::vec2(ndc.x, ndc.y) + 1.0f) * 0.5f;

                minScreen = glm::min(minScreen, screen);
                maxScreen = glm::max(maxScreen, screen);
                minDepth = std::min(minDepth, ndc.z);
            }
        }

        // If all corners behind camera, object might still be visible (straddling near plane)
        if (allBehindCamera) {
            return true;  // Conservative: assume visible
        }

        // Clamp to screen bounds
        minScreen = glm::clamp(minScreen, glm::vec2(0.0f), glm::vec2(1.0f));
        maxScreen = glm::clamp(maxScreen, glm::vec2(0.0f), glm::vec2(1.0f));

        // Calculate screen coverage to determine mip level
        glm::vec2 screenSize = maxScreen - minScreen;
        float maxDim = std::max(screenSize.x * width, screenSize.y * height);

        if (maxDim < 1.0f) {
            return true;  // Too small, assume visible
        }

        // Select mip level based on screen coverage
        uint32_t mipLevel = static_cast<uint32_t>(std::log2(maxDim));
        mipLevel = std::min(mipLevel, mipLevels - 1);

        // TODO: For full GPU-based culling, sample Hi-Z here
        // For now, return true (visible) - actual Hi-Z sampling would be done in GPU
        return true;
    }

    void HiZBuffer::cleanup() {
        if (!initialized) return;

        device.getLogicalDevice().waitIdle();

        device.getLogicalDevice().destroyDescriptorPool(descriptorPool);
        device.getLogicalDevice().destroyPipeline(computePipeline);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
        device.getLogicalDevice().destroyDescriptorSetLayout(descriptorSetLayout);

        device.getLogicalDevice().destroySampler(hiZSampler);
        device.getLogicalDevice().destroySampler(depthSampler);

        for (auto& view : mipViews) {
            device.getLogicalDevice().destroyImageView(view);
        }
        device.getLogicalDevice().destroyImageView(hiZImageView);
        device.getLogicalDevice().freeMemory(hiZMemory);
        device.getLogicalDevice().destroyImage(hiZImage);

        initialized = false;
    }

}
