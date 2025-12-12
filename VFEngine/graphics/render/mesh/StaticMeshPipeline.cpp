#include "StaticMeshPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/Utilities.hpp"
#include "resource/MeshResource.hpp"
#include "print/Logger.hpp"

namespace render::mesh
{
    StaticMeshPipeline::StaticMeshPipeline(core::Device& device, core::SwapChain& swapChain,
                                           core::OffscreenResources& offscreenResources)
        : device{device}, swapChain{swapChain}, offscreenResources{offscreenResources}
    {
        // Create command pool for buffer upload operations
        vk::CommandPoolCreateInfo commandPoolInfo;
        commandPoolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
        commandPoolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
        commandPool = device.getLogicalDevice().createCommandPoolUnique(commandPoolInfo);

        meshShader = std::make_shared<core::Shader>(device);
        meshShader->readShader("../../resources/shaders/mesh/mesh.glsl");
    }

    void StaticMeshPipeline::init(const ibl::ImageData& irradianceMap,
                                  const ibl::ImageData& prefilterMap,
                                  const ibl::ImageData& brdfLUT)
    {
        createRenderPass();
        createDescriptorSetLayout();
        createDescriptorPool();
        createCameraUBO();
        createDescriptorSet(irradianceMap, prefilterMap, brdfLUT);
        createPipelineLayout();
        createGraphicsPipeline();
        createFramebuffers();
    }

    void StaticMeshPipeline::initWithDefaults()
    {
        createRenderPass();
        createDescriptorSetLayout();
        createDescriptorPool();
        createCameraUBO();
        createDefaultIBLTextures();
        createDescriptorSet(defaultIrradiance, defaultPrefilter, defaultBrdfLUT);
        createPipelineLayout();
        createGraphicsPipeline();
        createFramebuffers();
        usingDefaultTextures = true;
    }

    void StaticMeshPipeline::createDefaultIBLTextures()
    {
        // Create simple 1x1 cubemap textures with neutral values for fallback PBR lighting
        const uint32_t size = 1;
        const uint32_t mipLevels = 1;

        auto createCubemap = [&](ibl::ImageData& imageData, std::array<float, 4> color) {
            // Create image
            vk::ImageCreateInfo imageInfo{};
            imageInfo.imageType = vk::ImageType::e2D;
            imageInfo.extent = vk::Extent3D{size, size, 1};
            imageInfo.mipLevels = mipLevels;
            imageInfo.arrayLayers = 6;  // Cubemap
            imageInfo.format = vk::Format::eR32G32B32A32Sfloat;
            imageInfo.tiling = vk::ImageTiling::eOptimal;
            imageInfo.initialLayout = vk::ImageLayout::eUndefined;
            imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
            imageInfo.samples = vk::SampleCountFlagBits::e1;
            imageInfo.sharingMode = vk::SharingMode::eExclusive;
            imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;

            imageData.image = device.getLogicalDevice().createImage(imageInfo);

            // Allocate memory
            vk::MemoryRequirements memRequirements = device.getLogicalDevice().getImageMemoryRequirements(imageData.image);
            vk::MemoryAllocateInfo allocInfo{};
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = core::Utilities::findMemoryType(device.getPhysicalDevice(),
                memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
            imageData.imageMemory = device.getLogicalDevice().allocateMemory(allocInfo);
            device.getLogicalDevice().bindImageMemory(imageData.image, imageData.imageMemory, 0);

            // Create staging buffer with color data for all 6 faces
            std::vector<float> pixels(6 * 4);  // 6 faces * 4 components (RGBA)
            for (int i = 0; i < 6; i++) {
                pixels[i * 4 + 0] = color[0];
                pixels[i * 4 + 1] = color[1];
                pixels[i * 4 + 2] = color[2];
                pixels[i * 4 + 3] = color[3];
            }

            vk::DeviceSize imageSize = pixels.size() * sizeof(float);
            vk::Buffer stagingBuffer;
            vk::DeviceMemory stagingMemory;

            core::BufferInfoRequest stagingRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            stagingRequest.size = imageSize;
            stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
            stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::Utilities::createBuffer(stagingRequest, stagingBuffer, stagingMemory);

            void* data;
            [[maybe_unused]] auto mapResult = device.getLogicalDevice().mapMemory(stagingMemory, 0, imageSize, {}, &data);
            memcpy(data, pixels.data(), imageSize);
            device.getLogicalDevice().unmapMemory(stagingMemory);

            // Transition image layout and copy data
            vk::CommandBufferAllocateInfo cmdAllocInfo{};
            cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
            cmdAllocInfo.commandPool = commandPool.get();
            cmdAllocInfo.commandBufferCount = 1;
            auto cmdBuffers = device.getLogicalDevice().allocateCommandBuffers(cmdAllocInfo);
            vk::CommandBuffer cmd = cmdBuffers[0];

            vk::CommandBufferBeginInfo beginInfo{};
            beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            cmd.begin(beginInfo);

            // Transition to transfer destination
            vk::ImageMemoryBarrier barrier{};
            barrier.oldLayout = vk::ImageLayout::eUndefined;
            barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = imageData.image;
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = mipLevels;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 6;
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                {}, nullptr, nullptr, barrier);

            // Copy buffer to image (all 6 faces)
            std::vector<vk::BufferImageCopy> copyRegions(6);
            for (uint32_t face = 0; face < 6; face++) {
                copyRegions[face].bufferOffset = face * 4 * sizeof(float);
                copyRegions[face].bufferRowLength = 0;
                copyRegions[face].bufferImageHeight = 0;
                copyRegions[face].imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
                copyRegions[face].imageSubresource.mipLevel = 0;
                copyRegions[face].imageSubresource.baseArrayLayer = face;
                copyRegions[face].imageSubresource.layerCount = 1;
                copyRegions[face].imageOffset = vk::Offset3D{0, 0, 0};
                copyRegions[face].imageExtent = vk::Extent3D{size, size, 1};
            }
            cmd.copyBufferToImage(stagingBuffer, imageData.image, vk::ImageLayout::eTransferDstOptimal, copyRegions);

            // Transition to shader read
            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                {}, nullptr, nullptr, barrier);

            cmd.end();

            vk::SubmitInfo submitInfo{};
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &cmd;
            device.getGraphicsQueue().submit(submitInfo);
            device.getGraphicsQueue().waitIdle();

            device.getLogicalDevice().freeCommandBuffers(commandPool.get(), cmd);
            device.getLogicalDevice().destroyBuffer(stagingBuffer);
            device.getLogicalDevice().freeMemory(stagingMemory);

            // Create image view
            vk::ImageViewCreateInfo viewInfo{};
            viewInfo.image = imageData.image;
            viewInfo.viewType = vk::ImageViewType::eCube;
            viewInfo.format = vk::Format::eR32G32B32A32Sfloat;
            viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = mipLevels;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 6;
            imageData.imageView = device.getLogicalDevice().createImageView(viewInfo);

            // Create sampler
            vk::SamplerCreateInfo samplerInfo{};
            samplerInfo.magFilter = vk::Filter::eLinear;
            samplerInfo.minFilter = vk::Filter::eLinear;
            samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
            samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.mipLodBias = 0.0f;
            samplerInfo.maxAnisotropy = 1.0f;
            samplerInfo.minLod = 0.0f;
            samplerInfo.maxLod = static_cast<float>(mipLevels);
            samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
            imageData.sampler = device.getLogicalDevice().createSampler(samplerInfo);
        };

        // Create 2D texture for BRDF LUT (not a cubemap)
        auto create2DTexture = [&](ibl::ImageData& imageData) {
            vk::ImageCreateInfo imageInfo{};
            imageInfo.imageType = vk::ImageType::e2D;
            imageInfo.extent = vk::Extent3D{size, size, 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = vk::Format::eR32G32B32A32Sfloat;
            imageInfo.tiling = vk::ImageTiling::eOptimal;
            imageInfo.initialLayout = vk::ImageLayout::eUndefined;
            imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
            imageInfo.samples = vk::SampleCountFlagBits::e1;
            imageInfo.sharingMode = vk::SharingMode::eExclusive;

            imageData.image = device.getLogicalDevice().createImage(imageInfo);

            vk::MemoryRequirements memRequirements = device.getLogicalDevice().getImageMemoryRequirements(imageData.image);
            vk::MemoryAllocateInfo allocInfo{};
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = core::Utilities::findMemoryType(device.getPhysicalDevice(),
                memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
            imageData.imageMemory = device.getLogicalDevice().allocateMemory(allocInfo);
            device.getLogicalDevice().bindImageMemory(imageData.image, imageData.imageMemory, 0);

            // Default BRDF LUT value (white = full reflection)
            std::array<float, 4> pixel = {1.0f, 1.0f, 1.0f, 1.0f};
            vk::DeviceSize imageSize = sizeof(pixel);
            vk::Buffer stagingBuffer;
            vk::DeviceMemory stagingMemory;

            core::BufferInfoRequest stagingRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            stagingRequest.size = imageSize;
            stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
            stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::Utilities::createBuffer(stagingRequest, stagingBuffer, stagingMemory);

            void* data;
            [[maybe_unused]] auto mapResult2 = device.getLogicalDevice().mapMemory(stagingMemory, 0, imageSize, {}, &data);
            memcpy(data, pixel.data(), imageSize);
            device.getLogicalDevice().unmapMemory(stagingMemory);

            vk::CommandBufferAllocateInfo cmdAllocInfo{};
            cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
            cmdAllocInfo.commandPool = commandPool.get();
            cmdAllocInfo.commandBufferCount = 1;
            auto cmdBuffers = device.getLogicalDevice().allocateCommandBuffers(cmdAllocInfo);
            vk::CommandBuffer cmd = cmdBuffers[0];

            vk::CommandBufferBeginInfo beginInfo{};
            beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            cmd.begin(beginInfo);

            vk::ImageMemoryBarrier barrier{};
            barrier.oldLayout = vk::ImageLayout::eUndefined;
            barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = imageData.image;
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                {}, nullptr, nullptr, barrier);

            vk::BufferImageCopy copyRegion{};
            copyRegion.bufferOffset = 0;
            copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            copyRegion.imageSubresource.mipLevel = 0;
            copyRegion.imageSubresource.baseArrayLayer = 0;
            copyRegion.imageSubresource.layerCount = 1;
            copyRegion.imageExtent = vk::Extent3D{size, size, 1};
            cmd.copyBufferToImage(stagingBuffer, imageData.image, vk::ImageLayout::eTransferDstOptimal, copyRegion);

            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                {}, nullptr, nullptr, barrier);

            cmd.end();

            vk::SubmitInfo submitInfo{};
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &cmd;
            device.getGraphicsQueue().submit(submitInfo);
            device.getGraphicsQueue().waitIdle();

            device.getLogicalDevice().freeCommandBuffers(commandPool.get(), cmd);
            device.getLogicalDevice().destroyBuffer(stagingBuffer);
            device.getLogicalDevice().freeMemory(stagingMemory);

            vk::ImageViewCreateInfo viewInfo{};
            viewInfo.image = imageData.image;
            viewInfo.viewType = vk::ImageViewType::e2D;
            viewInfo.format = vk::Format::eR32G32B32A32Sfloat;
            viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
            imageData.imageView = device.getLogicalDevice().createImageView(viewInfo);

            vk::SamplerCreateInfo samplerInfo{};
            samplerInfo.magFilter = vk::Filter::eLinear;
            samplerInfo.minFilter = vk::Filter::eLinear;
            samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
            samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.mipLodBias = 0.0f;
            samplerInfo.maxAnisotropy = 1.0f;
            samplerInfo.minLod = 0.0f;
            samplerInfo.maxLod = 1.0f;
            samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
            imageData.sampler = device.getLogicalDevice().createSampler(samplerInfo);
        };

        // Irradiance: neutral ambient light (gray)
        createCubemap(defaultIrradiance, {0.3f, 0.3f, 0.3f, 1.0f});
        // Prefilter: same neutral value
        createCubemap(defaultPrefilter, {0.3f, 0.3f, 0.3f, 1.0f});
        // BRDF LUT: 2D texture
        create2DTexture(defaultBrdfLUT);
    }

    void StaticMeshPipeline::recreate()
    {
        // Cleanup framebuffers and render pass for recreation
        for (auto& framebuffer : framebuffers)
        {
            device.getLogicalDevice().destroyFramebuffer(framebuffer);
        }
        device.getLogicalDevice().destroyRenderPass(renderPass);
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);

        createRenderPass();
        createGraphicsPipeline();
        createFramebuffers();
    }

    void StaticMeshPipeline::createRenderPass()
    {
        // Color attachment - load existing content (preserve skybox)
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChain.getSwapchainImageFormat();
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;  // Preserve skybox
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        // Depth attachment
        vk::AttachmentDescription depthAttachment{};
        depthAttachment.format = swapChain.getSwapchainDepthStencilFormat();
        depthAttachment.samples = vk::SampleCountFlagBits::e1;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        std::array<vk::AttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);
    }

    void StaticMeshPipeline::createDescriptorSetLayout()
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings(4);

        // Binding 0: Camera UBO (vertex + fragment)
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        bindings[0].pImmutableSamplers = nullptr;

        // Binding 1: Irradiance cubemap (fragment only)
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[1].pImmutableSamplers = nullptr;

        // Binding 2: Prefilter cubemap (fragment only)
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[2].pImmutableSamplers = nullptr;

        // Binding 3: BRDF LUT (fragment only)
        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[3].pImmutableSamplers = nullptr;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void StaticMeshPipeline::createDescriptorPool()
    {
        std::vector<vk::DescriptorPoolSize> poolSizes(2);
        poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = 3;  // irradiance, prefilter, brdfLUT

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void StaticMeshPipeline::createCameraUBO()
    {
        core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent;
        bufferRequest.size = sizeof(CameraUBO);
        core::Utilities::createBuffer(bufferRequest, cameraUBO, cameraUBOMemory);
    }

    void StaticMeshPipeline::createDescriptorSet(const ibl::ImageData& irradianceMap,
                                                  const ibl::ImageData& prefilterMap,
                                                  const ibl::ImageData& brdfLUT)
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        // Camera UBO binding
        vk::DescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = cameraUBO;
        uboBufferInfo.offset = 0;
        uboBufferInfo.range = sizeof(CameraUBO);

        vk::WriteDescriptorSet uboWrite{};
        uboWrite.dstSet = descriptorSet;
        uboWrite.dstBinding = 0;
        uboWrite.dstArrayElement = 0;
        uboWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        uboWrite.descriptorCount = 1;
        uboWrite.pBufferInfo = &uboBufferInfo;

        // Irradiance map binding
        vk::DescriptorImageInfo irradianceImageInfo{};
        irradianceImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        irradianceImageInfo.imageView = irradianceMap.imageView;
        irradianceImageInfo.sampler = irradianceMap.sampler;

        vk::WriteDescriptorSet irradianceWrite{};
        irradianceWrite.dstSet = descriptorSet;
        irradianceWrite.dstBinding = 1;
        irradianceWrite.dstArrayElement = 0;
        irradianceWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        irradianceWrite.descriptorCount = 1;
        irradianceWrite.pImageInfo = &irradianceImageInfo;

        // Prefilter map binding
        vk::DescriptorImageInfo prefilterImageInfo{};
        prefilterImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        prefilterImageInfo.imageView = prefilterMap.imageView;
        prefilterImageInfo.sampler = prefilterMap.sampler;

        vk::WriteDescriptorSet prefilterWrite{};
        prefilterWrite.dstSet = descriptorSet;
        prefilterWrite.dstBinding = 2;
        prefilterWrite.dstArrayElement = 0;
        prefilterWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        prefilterWrite.descriptorCount = 1;
        prefilterWrite.pImageInfo = &prefilterImageInfo;

        // BRDF LUT binding
        vk::DescriptorImageInfo brdfImageInfo{};
        brdfImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        brdfImageInfo.imageView = brdfLUT.imageView;
        brdfImageInfo.sampler = brdfLUT.sampler;

        vk::WriteDescriptorSet brdfWrite{};
        brdfWrite.dstSet = descriptorSet;
        brdfWrite.dstBinding = 3;
        brdfWrite.dstArrayElement = 0;
        brdfWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        brdfWrite.descriptorCount = 1;
        brdfWrite.pImageInfo = &brdfImageInfo;

        std::array<vk::WriteDescriptorSet, 4> descriptorWrites = {
            uboWrite, irradianceWrite, prefilterWrite, brdfWrite
        };
        device.getLogicalDevice().updateDescriptorSets(descriptorWrites, nullptr);
    }

    void StaticMeshPipeline::createPipelineLayout()
    {
        // Push constant range for MeshPushConstants
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(MeshPushConstants);

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);
    }

    void StaticMeshPipeline::createGraphicsPipeline()
    {
        // Vertex input state - using MeshVertexInput helper
        auto bindingDescription = MeshVertexInput::getBindingDescription();
        auto attributeDescriptions = MeshVertexInput::getAttributeDescriptions();

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        // Input assembly
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // Viewport and scissor
        vk::Viewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapChain.getSwapchainExtent().width);
        viewport.height = static_cast<float>(swapChain.getSwapchainExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.offset = vk::Offset2D(0, 0);
        scissor.extent = swapChain.getSwapchainExtent();

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        // Rasterizer - back-face culling enabled for meshes
        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eBack;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizer.depthBiasEnable = VK_FALSE;

        // Multisampling
        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        // Depth testing - enabled for meshes
        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = vk::CompareOp::eLess;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // Color blending - no blending
        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                              vk::ColorComponentFlagBits::eG |
                                              vk::ColorComponentFlagBits::eB |
                                              vk::ColorComponentFlagBits::eA;
        colorBlendAttachment.blendEnable = VK_FALSE;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // Create pipeline
        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(meshShader->getShaderStages().size());
        pipelineInfo.pStages = meshShader->getShaderStages().data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        graphicsPipeline = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;
    }

    void StaticMeshPipeline::createFramebuffers()
    {
        framebuffers.resize(offscreenResources.colorImages.size());
        vk::ImageView depth = offscreenResources.depthImage.depthImageView;

        for (uint32_t i = 0; i < framebuffers.size(); i++)
        {
            vk::ImageView colorView = offscreenResources.colorImages[i].colorImageView;
            std::array<vk::ImageView, 2> attachments = {colorView, depth};

            vk::FramebufferCreateInfo framebufferInfo{};
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = swapChain.getSwapchainExtent().width;
            framebufferInfo.height = swapChain.getSwapchainExtent().height;
            framebufferInfo.layers = 1;

            framebuffers[i] = device.getLogicalDevice().createFramebuffer(framebufferInfo);
        }
    }

    void StaticMeshPipeline::updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                              const glm::vec3& cameraPos) const
    {
        CameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;

        void* data;
        vk::Result result = device.getLogicalDevice().mapMemory(cameraUBOMemory, 0, sizeof(ubo), {}, &data);
        if (result == vk::Result::eSuccess)
        {
            memcpy(data, &ubo, sizeof(ubo));
            device.getLogicalDevice().unmapMemory(cameraUBOMemory);
        }
    }

    void StaticMeshPipeline::cleanUpForReinit()
    {
        // Clean up pipeline/descriptor resources but preserve loaded meshes and command pool

        for (auto& framebuffer : framebuffers)
        {
            device.getLogicalDevice().destroyFramebuffer(framebuffer);
        }
        framebuffers.clear();

        device.getLogicalDevice().destroyBuffer(cameraUBO);
        device.getLogicalDevice().freeMemory(cameraUBOMemory);
        cameraUBO = nullptr;
        cameraUBOMemory = nullptr;

        device.getLogicalDevice().destroyRenderPass(renderPass);
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
        device.getLogicalDevice().freeDescriptorSets(descriptorPool, descriptorSet);
        device.getLogicalDevice().destroyDescriptorPool(descriptorPool);
        device.getLogicalDevice().destroyDescriptorSetLayout(descriptorSetLayout);

        renderPass = nullptr;
        graphicsPipeline = nullptr;
        pipelineLayout = nullptr;
        descriptorSet = nullptr;
        descriptorPool = nullptr;
        descriptorSetLayout = nullptr;

        // Clean up default textures if we created them
        if (usingDefaultTextures)
        {
            auto cleanupImageData = [&](ibl::ImageData& imageData) {
                if (imageData.sampler) device.getLogicalDevice().destroySampler(imageData.sampler);
                if (imageData.imageView) device.getLogicalDevice().destroyImageView(imageData.imageView);
                if (imageData.image) device.getLogicalDevice().destroyImage(imageData.image);
                if (imageData.imageMemory) device.getLogicalDevice().freeMemory(imageData.imageMemory);
                imageData = {};
            };

            cleanupImageData(defaultIrradiance);
            cleanupImageData(defaultPrefilter);
            cleanupImageData(defaultBrdfLUT);
            usingDefaultTextures = false;
        }
    }

    void StaticMeshPipeline::cleanUp()
    {
        // Unload all meshes first
        unloadAllMeshes();

        // Clean up pipeline/descriptor resources
        cleanUpForReinit();

        // Reset command pool (automatic cleanup via UniqueCommandPool)
        commandPool.reset();
    }

    void StaticMeshPipeline::cleanUpShader()
    {
        meshShader->cleanUp();
    }

    std::string StaticMeshPipeline::loadMesh(std::string_view meshPath)
    {
        std::string pathStr(meshPath);
        
        if (loadedMeshes.contains(pathStr))
        {
            return pathStr;
        }
        
        auto meshFuture = resource::ResourceManager::loadMeshAsync(meshPath);
        auto meshesDataPtr = meshFuture.get();

        if (!meshesDataPtr || meshesDataPtr->meshes.empty())
        {
            loggerError("Failed to load mesh from: {}", meshPath);
            return "";
        }

        // For now, combine all submeshes into one (or just use first mesh)
        // In future, could return multiple mesh IDs
        const auto& meshData = meshesDataPtr->meshes[0];

        if (meshData.vertices.empty())
        {
            loggerError("Mesh has no vertices: {}", meshPath);
            return "";
        }

        MeshGPUData gpuData{};
        gpuData.sourcePath = pathStr;
        gpuData.vertexCount = static_cast<uint32_t>(meshData.vertices.size());
        gpuData.indexCount = static_cast<uint32_t>(meshData.indices.size());

        // Create vertex buffer (device local for best performance)
        vk::DeviceSize vertexBufferSize = sizeof(resource::Vertex) * meshData.vertices.size();

        core::BufferInfoRequest vertexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexBufferRequest.size = vertexBufferSize;
        vertexBufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::Utilities::createBuffer(vertexBufferRequest, gpuData.vertexBuffer, gpuData.vertexBufferMemory);

        // Copy vertex data to GPU using staging buffer
        core::Utilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            commandPool.get(),
            gpuData.vertexBuffer,
            meshData.vertices.data(),
            vertexBufferSize
        );

        // Create index buffer if indices exist
        if (!meshData.indices.empty())
        {
            vk::DeviceSize indexBufferSize = sizeof(uint32_t) * meshData.indices.size();

            core::BufferInfoRequest indexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            indexBufferRequest.size = indexBufferSize;
            indexBufferRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
            indexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(indexBufferRequest, gpuData.indexBuffer, gpuData.indexBufferMemory);

            // Copy index data to GPU
            core::Utilities::copyToBuffer(
                device.getLogicalDevice(),
                device.getPhysicalDevice(),
                device.getGraphicsQueue(),
                commandPool.get(),
                gpuData.indexBuffer,
                meshData.indices.data(),
                indexBufferSize
            );
        }

        loadedMeshes[pathStr] = gpuData;
        loggerInfo("Loaded mesh: {} ({} vertices, {} indices)", meshPath, gpuData.vertexCount, gpuData.indexCount);

        return pathStr;
    }

    void StaticMeshPipeline::unloadMesh(const std::string& meshId)
    {
        auto it = loadedMeshes.find(meshId);
        if (it == loadedMeshes.end())
        {
            return;
        }

        const auto& gpuData = it->second;

        // Wait for device to finish using the buffers
        device.getLogicalDevice().waitIdle();

        // Destroy vertex buffer
        if (gpuData.vertexBuffer)
        {
            device.getLogicalDevice().destroyBuffer(gpuData.vertexBuffer);
            device.getLogicalDevice().freeMemory(gpuData.vertexBufferMemory);
        }

        // Destroy index buffer
        if (gpuData.indexBuffer)
        {
            device.getLogicalDevice().destroyBuffer(gpuData.indexBuffer);
            device.getLogicalDevice().freeMemory(gpuData.indexBufferMemory);
        }

        loadedMeshes.erase(it);
        loggerInfo("Unloaded mesh: {}", meshId);
    }

    void StaticMeshPipeline::unloadAllMeshes()
    {
        if (loadedMeshes.empty())
        {
            return;
        }

        // Wait for device to finish
        device.getLogicalDevice().waitIdle();

        for (auto& [path, gpuData] : loadedMeshes)
        {
            if (gpuData.vertexBuffer)
            {
                device.getLogicalDevice().destroyBuffer(gpuData.vertexBuffer);
                device.getLogicalDevice().freeMemory(gpuData.vertexBufferMemory);
            }
            if (gpuData.indexBuffer)
            {
                device.getLogicalDevice().destroyBuffer(gpuData.indexBuffer);
                device.getLogicalDevice().freeMemory(gpuData.indexBufferMemory);
            }
        }

        loadedMeshes.clear();
        loggerInfo("Unloaded all meshes");
    }

    const MeshGPUData* StaticMeshPipeline::getMesh(const std::string& meshId) const
    {
        auto it = loadedMeshes.find(meshId);
        if (it != loadedMeshes.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    bool StaticMeshPipeline::isMeshLoaded(const std::string& meshId) const
    {
        return loadedMeshes.contains(meshId);
    }

    std::vector<std::string> StaticMeshPipeline::getLoadedMeshIds() const
    {
        std::vector<std::string> ids;
        ids.reserve(loadedMeshes.size());
        for (const auto& [path, _] : loadedMeshes)
        {
            ids.push_back(path);
        }
        return ids;
    }

    void StaticMeshPipeline::recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                                  uint32_t imageIndex,
                                                  const std::vector<MeshRenderData>& meshDrawList) const
    {
        if (meshDrawList.empty())
        {
            return;
        }

        vk::RenderPassBeginInfo renderPassInfo{};
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[imageIndex];
        renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderPassInfo.renderArea.extent = swapChain.getSwapchainExtent();

        // Clear values for depth only - color uses loadOp::eLoad to preserve skybox
        std::array<vk::ClearValue, 2> clearValues{};
        clearValues[0].color = vk::ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 1.0f}};
        clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        // Bind pipeline and descriptor set once for all meshes
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                         pipelineLayout, 0, descriptorSet, nullptr);

        // Render each mesh in the draw list
        for (const auto& meshData : meshDrawList)
        {
            const MeshGPUData* gpuData = getMesh(meshData.meshPath);
            if (!gpuData)
            {
                continue;  // Skip meshes that aren't loaded
            }

            // Setup push constants with transform and material properties
            MeshPushConstants pushConstants{};
            pushConstants.model = meshData.modelMatrix;
            pushConstants.albedo = meshData.albedo;
            pushConstants.metallic = meshData.metallic;
            pushConstants.roughness = meshData.roughness;
            pushConstants.ao = meshData.ao;
            pushConstants.padding = 0.0f;

            commandBuffer.pushConstants(pipelineLayout,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0, sizeof(MeshPushConstants), &pushConstants);

            // Bind vertex buffer
            vk::Buffer vertexBuffers[] = {gpuData->vertexBuffer};
            vk::DeviceSize offsets[] = {0};
            commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

            // Draw with indices if available, otherwise draw vertices directly
            if (gpuData->indexCount > 0)
            {
                commandBuffer.bindIndexBuffer(gpuData->indexBuffer, 0, vk::IndexType::eUint32);
                commandBuffer.drawIndexed(gpuData->indexCount, 1, 0, 0, 0);
            }
            else
            {
                commandBuffer.draw(gpuData->vertexCount, 1, 0, 0);
            }
        }

        commandBuffer.endRenderPass();
    }
}
