#include "WaterRefractionResources.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/Device.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include <cstring>

namespace render::water
{
    WaterRefractionResources::WaterRefractionResources(core::Device& device)
        : device(device)
    {
    }

    WaterRefractionResources::~WaterRefractionResources()
    {
        cleanup();
    }

    void WaterRefractionResources::init(vk::Format swapchainFormat, uint32_t width, uint32_t height,
                                        vk::ImageView sceneDepthView,
                                        vk::ImageView shoreDepthView, vk::Sampler shoreDepthSampler,
                                        vk::ImageView rippleView, vk::Sampler rippleSampler)
    {
        createRefractionImage(swapchainFormat, width, height);
        createSampler();
        createParamsBuffer();
        createDescriptorLayout();
        createDescriptorPool();
        allocateDescriptorSet();
        updateDescriptorSet(sceneDepthView, shoreDepthView, shoreDepthSampler, rippleView, rippleSampler);
        transitionImageInitial();
        initialized = true;
    }

    void WaterRefractionResources::recreate(vk::Format swapchainFormat, uint32_t width, uint32_t height,
                                            vk::ImageView sceneDepthView,
                                            vk::ImageView shoreDepthView, vk::Sampler shoreDepthSampler,
                                            vk::ImageView rippleView, vk::Sampler rippleSampler)
    {
        cleanup();
        init(swapchainFormat, width, height, sceneDepthView, shoreDepthView, shoreDepthSampler,
             rippleView, rippleSampler);
    }

    void WaterRefractionResources::cleanup()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        if (descriptorPool)
        {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (descriptorSetLayout) { vkDevice.destroyDescriptorSetLayout(descriptorSetLayout); descriptorSetLayout = nullptr; }
        if (depthSampler)        { vkDevice.destroySampler(depthSampler); depthSampler = nullptr; }
        if (refractionSampler)   { vkDevice.destroySampler(refractionSampler); refractionSampler = nullptr; }
        if (refractionView)      { vkDevice.destroyImageView(refractionView); refractionView = nullptr; }
        if (refractionImage)     { vkDevice.destroyImage(refractionImage); refractionImage = nullptr; }
        if (refractionAllocation) { device.getMemoryManager().free(refractionAllocation); refractionAllocation = {}; }

        if (paramsBuffer)
        {
            paramsMapped = nullptr;
            core::BufferUtilities::destroyBuffer(vkDevice, paramsBuffer, paramsAllocation, device.getMemoryManager());
            paramsBuffer = nullptr;
            paramsAllocation = {};
        }

        initialized = false;
    }

    void WaterRefractionResources::createRefractionImage(vk::Format format, uint32_t width, uint32_t height)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        core::ImageInfoRequest req(vkDevice, device.getPhysicalDevice(),
            width, height, 1, 1,
            format,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        core::ImageUtilities::createImage(req, refractionImage, refractionAllocation, device.getMemoryManager());

        core::ImageViewInfoRequest viewReq(vkDevice, refractionImage,
            format,
            vk::ImageAspectFlagBits::eColor, vk::ImageViewType::e2D);
        core::ImageUtilities::createImageView(viewReq, refractionView);
    }

    void WaterRefractionResources::createSampler()
    {
        vk::SamplerCreateInfo info{};
        info.magFilter = vk::Filter::eLinear;
        info.minFilter = vk::Filter::eLinear;
        info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        info.anisotropyEnable = VK_FALSE;
        info.maxAnisotropy = 1.0f;
        info.mipmapMode = vk::SamplerMipmapMode::eLinear;

        refractionSampler = device.getLogicalDevice().createSampler(info);

        // VK-1604: separate nearest sampler for the scene depth binding (see header note).
        vk::SamplerCreateInfo depthInfo{};
        depthInfo.magFilter = vk::Filter::eNearest;
        depthInfo.minFilter = vk::Filter::eNearest;
        depthInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        depthInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        depthInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        depthInfo.anisotropyEnable = VK_FALSE;
        depthInfo.maxAnisotropy = 1.0f;
        depthInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;

        depthSampler = device.getLogicalDevice().createSampler(depthInfo);
    }

    void WaterRefractionResources::createParamsBuffer()
    {
        core::BufferInfoRequest req(device.getLogicalDevice(), device.getPhysicalDevice());
        req.size = sizeof(WaterExtendedParams);
        req.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        req.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(req, paramsBuffer, paramsAllocation, device.getMemoryManager());
        paramsMapped = paramsAllocation.mappedPtr;

        // Start from the struct defaults so the first frames before updateWater runs (and any
        // frame where the ocean is disabled) read sane, all-features-off values.
        if (paramsMapped)
        {
            WaterExtendedParams defaults{};
            std::memcpy(paramsMapped, &defaults, sizeof(WaterExtendedParams));
        }
    }

    void WaterRefractionResources::updateParams(const WaterExtendedParams& params)
    {
        if (paramsMapped)
            std::memcpy(paramsMapped, &params, sizeof(WaterExtendedParams));
    }

    // NOTE: WaterPipeline::createRefractionDummy() mirrors this layout for the no-refraction /
    // RTT path. Binding count, types and STAGE FLAGS must match exactly or the two layouts are
    // not descriptor-set-compatible and binding the dummy into a pipeline built from this layout
    // is a validation error.
    void WaterRefractionResources::createDescriptorLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 5> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

        // VK-1604: extended params. Vertex too — hex tiling runs in the vertex stage.
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

        // VK-1605: shore-depth field. The vertex stage is the one that matters (shoaling and the
        // breaking deformer displace geometry); the fragment stage shares the same include and
        // gets it for free.
        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

        // VK-1606: the interactive ripple patch. Vertex displaces the surface with its height,
        // fragment adds its foam, so both stages again.
        bindings[4].binding = 4;
        bindings[4].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
    }

    void WaterRefractionResources::createDescriptorPool()
    {
        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = 4;   // colour + depth + shore depth (VK-1605) + ripple (VK-1606)
        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void WaterRefractionResources::allocateDescriptorSet()
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
    }

    void WaterRefractionResources::updateDescriptorSet(vk::ImageView sceneDepthView,
                                                       vk::ImageView shoreDepthView,
                                                       vk::Sampler shoreDepthSampler,
                                                       vk::ImageView rippleView,
                                                       vk::Sampler rippleSampler)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorImageInfo, 4> imageInfos{};
        imageInfos[0].sampler = refractionSampler;
        imageInfos[0].imageView = refractionView;
        imageInfos[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        // VK-1604: binding 1 is the real scene depth image. water.glsl reconstructs linear depth
        // from it for shore foam, SSR and Beer-Lambert absorption. Legal because the water draw
        // runs inside beginWaterReadOnlyDepthPassGraphManaged, which binds depth read-only.
        // Fall back to the color copy only if no depth view was supplied (init ordering safety).
        if (sceneDepthView)
        {
            imageInfos[1].sampler = depthSampler;
            imageInfos[1].imageView = sceneDepthView;
            imageInfos[1].imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        }
        else
        {
            imageInfos[1].sampler = refractionSampler;
            imageInfos[1].imageView = refractionView;
            imageInfos[1].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }

        // VK-1605: binding 3, the shore-depth field. Falls back to the refraction colour copy when
        // no shore resources exist yet (init ordering safety) — the shoaling flags stay clear in
        // that case, so nothing ever reads it.
        if (shoreDepthView && shoreDepthSampler)
        {
            imageInfos[2].sampler = shoreDepthSampler;
            imageInfos[2].imageView = shoreDepthView;
        }
        else
        {
            imageInfos[2].sampler = refractionSampler;
            imageInfos[2].imageView = refractionView;
        }
        imageInfos[2].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        // VK-1606: binding 4, the ripple patch. Same init-ordering fallback as the shore field —
        // the ripple flag stays clear until the sim has actually run, so nothing ever reads it.
        if (rippleView && rippleSampler)
        {
            imageInfos[3].sampler = rippleSampler;
            imageInfos[3].imageView = rippleView;
        }
        else
        {
            imageInfos[3].sampler = refractionSampler;
            imageInfos[3].imageView = refractionView;
        }
        imageInfos[3].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::DescriptorBufferInfo paramsInfo{};
        paramsInfo.buffer = paramsBuffer;
        paramsInfo.offset = 0;
        paramsInfo.range = sizeof(WaterExtendedParams);

        std::array<vk::WriteDescriptorSet, 5> writes{};
        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[0].pImageInfo = &imageInfos[0];

        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[1].pImageInfo = &imageInfos[1];

        writes[2].dstSet = descriptorSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[2].pBufferInfo = &paramsInfo;

        writes[3].dstSet = descriptorSet;
        writes[3].dstBinding = 3;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[3].pImageInfo = &imageInfos[2];

        writes[4].dstSet = descriptorSet;
        writes[4].dstBinding = 4;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[4].pImageInfo = &imageInfos[3];

        vkDevice.updateDescriptorSets(writes, nullptr);
    }

    void WaterRefractionResources::transitionImageInitial()
    {
        auto cmd = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), device.getStagingCommandPool());

        core::ImageUtilities::transitionImageLayout(
            cmd.get(), refractionImage,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::Utilities::endSingleTimeCommands(device, cmd);
    }

    void WaterRefractionResources::copySceneColor(vk::CommandBuffer cmd, vk::Image srcColorImage, uint32_t width, uint32_t height)
    {
        // 1. Transition refraction image: ShaderReadOnly -> TransferDst
        vk::ImageMemoryBarrier toTransferDst{};
        toTransferDst.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        toTransferDst.newLayout = vk::ImageLayout::eTransferDstOptimal;
        toTransferDst.image = refractionImage;
        toTransferDst.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        toTransferDst.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        toTransferDst.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                            vk::PipelineStageFlagBits::eTransfer,
                            {}, {}, {}, toTransferDst);

        // 2. Transition source: ColorAttachmentOptimal -> TransferSrc
        vk::ImageMemoryBarrier srcToTransfer{};
        srcToTransfer.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
        srcToTransfer.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        srcToTransfer.image = srcColorImage;
        srcToTransfer.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        srcToTransfer.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        srcToTransfer.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                            vk::PipelineStageFlagBits::eTransfer,
                            {}, {}, {}, srcToTransfer);

        // 3. Copy
        vk::ImageCopy copyRegion{};
        copyRegion.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        copyRegion.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        copyRegion.extent = vk::Extent3D {width, height, 1};
        cmd.copyImage(srcColorImage, vk::ImageLayout::eTransferSrcOptimal,
                      refractionImage, vk::ImageLayout::eTransferDstOptimal,
                      copyRegion);

        // 4. Transition refraction: TransferDst -> ShaderReadOnly
        vk::ImageMemoryBarrier toShaderRead{};
        toShaderRead.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        toShaderRead.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        toShaderRead.image = refractionImage;
        toShaderRead.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        toShaderRead.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        toShaderRead.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eFragmentShader,
                            {}, {}, {}, toShaderRead);

        // 5. Transition source: TransferSrc -> ColorAttachment
        vk::ImageMemoryBarrier srcBack{};
        srcBack.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        srcBack.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
        srcBack.image = srcColorImage;
        srcBack.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        srcBack.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        srcBack.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eColorAttachmentOutput,
                            {}, {}, {}, srcBack);
    }
}
