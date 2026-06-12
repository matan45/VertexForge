#include "ReactiveMaskPass.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"

#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::upscaling
{
    ReactiveMaskPass::ReactiveMaskPass(core::Device& device)
        : device(device)
    {
    }

    ReactiveMaskPass::~ReactiveMaskPass()
    {
        cleanup();
    }

    void ReactiveMaskPass::init()
    {
        if (initialized) return;

        createSampler();
        createDescriptorLayout();
        createDescriptorPool();
        allocateDescriptorSets();
        createComputePipeline();

        initialized = true;
        vfLogInfo("ReactiveMaskPass: Initialized");
    }

    void ReactiveMaskPass::cleanup()
    {
        if (!initialized) return;
        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        vkDevice.destroyPipeline(computePipeline);
        vkDevice.destroyPipelineLayout(pipelineLayout);
        vkDevice.destroyDescriptorPool(descriptorPool);
        vkDevice.destroyDescriptorSetLayout(descriptorSetLayout);
        vkDevice.destroySampler(colorSampler);

        shader.reset();
        initialized = false;
    }

    void ReactiveMaskPass::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eNearest;
        samplerInfo.minFilter = vk::Filter::eNearest;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;

        colorSampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void ReactiveMaskPass::createDescriptorLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // binding 0: opaque color, binding 1: final color, binding 2: mask storage image
        std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};
        bindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        bindings[1] = {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        bindings[2] = {2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute};

        std::array<vk::DescriptorBindingFlags, 3> bindingFlags{};
        bindingFlags[0] = vk::DescriptorBindingFlagBits::eUpdateAfterBind;
        bindingFlags[1] = vk::DescriptorBindingFlagBits::eUpdateAfterBind;
        bindingFlags[2] = vk::DescriptorBindingFlagBits::eUpdateAfterBind;

        vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
        flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
        flagsInfo.pBindingFlags = bindingFlags.data();

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        layoutInfo.pNext = &flagsInfo;
        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
    }

    void ReactiveMaskPass::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eCombinedImageSampler, 2 * core::MAX_FRAMES_IN_FLIGHT};
        poolSizes[1] = {vk::DescriptorType::eStorageImage, core::MAX_FRAMES_IN_FLIGHT};

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
        poolInfo.maxSets = core::MAX_FRAMES_IN_FLIGHT;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
    }

    void ReactiveMaskPass::allocateDescriptorSets()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        for (uint32_t i = 0; i < core::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &descriptorSetLayout;
            descriptorSets[i] = vkDevice.allocateDescriptorSets(allocInfo)[0];
        }
    }

    void ReactiveMaskPass::createComputePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/postprocess/reactive_mask.glsl");

        if (shader->getShaderStages().empty())
        {
            vfLogError("ReactiveMaskPass: Failed to compile shader: {}", shader->getLastCompilationError());
            return;
        }

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = shader->getShaderStages()[0];
        pipelineInfo.layout = pipelineLayout;
        computePipeline = core::PipelineUtilities::createComputePipeline(vkDevice, pipelineInfo);
    }

    void ReactiveMaskPass::dispatch(vk::CommandBuffer cmd,
                                    vk::ImageView opaqueColorView,
                                    vk::ImageView finalColorView,
                                    vk::ImageView maskStorageView,
                                    vk::Image maskImage,
                                    uint32_t width, uint32_t height,
                                    uint32_t frameIndex)
    {
        if (!initialized) return;

        uint32_t frame = frameIndex % core::MAX_FRAMES_IN_FLIGHT;
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorImageInfo opaqueInfo{};
        opaqueInfo.sampler = colorSampler;
        opaqueInfo.imageView = opaqueColorView;
        opaqueInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::DescriptorImageInfo finalInfo{};
        finalInfo.sampler = colorSampler;
        finalInfo.imageView = finalColorView;
        finalInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::DescriptorImageInfo maskInfo{};
        maskInfo.imageView = maskStorageView;
        maskInfo.imageLayout = vk::ImageLayout::eGeneral;

        std::array<vk::WriteDescriptorSet, 3> writes{};
        writes[0].dstSet = descriptorSets[frame];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[0].pImageInfo = &opaqueInfo;

        writes[1].dstSet = descriptorSets[frame];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[1].pImageInfo = &finalInfo;

        writes[2].dstSet = descriptorSets[frame];
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = vk::DescriptorType::eStorageImage;
        writes[2].pImageInfo = &maskInfo;

        vkDevice.updateDescriptorSets(writes, {});

        // Mask contents are fully rewritten - discard via Undefined
        vk::ImageMemoryBarrier toGeneral{};
        toGeneral.oldLayout = vk::ImageLayout::eUndefined;
        toGeneral.newLayout = vk::ImageLayout::eGeneral;
        toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.image = maskImage;
        toGeneral.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        toGeneral.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        toGeneral.dstAccessMask = vk::AccessFlagBits::eShaderWrite;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, {}, toGeneral);

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout,
                               0, descriptorSets[frame], {});

        uint32_t groupsX = (width + 7) / 8;
        uint32_t groupsY = (height + 7) / 8;
        cmd.dispatch(groupsX, groupsY, 1);

        vk::ImageMemoryBarrier toRead{};
        toRead.oldLayout = vk::ImageLayout::eGeneral;
        toRead.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.image = maskImage;
        toRead.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        toRead.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        toRead.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, {}, toRead);
    }
}
