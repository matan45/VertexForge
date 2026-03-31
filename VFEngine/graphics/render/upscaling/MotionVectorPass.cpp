#include "MotionVectorPass.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"

#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::upscaling
{
    MotionVectorPass::MotionVectorPass(core::Device& device)
        : device(device)
    {
    }

    MotionVectorPass::~MotionVectorPass()
    {
        cleanup();
    }

    void MotionVectorPass::init()
    {
        if (initialized) return;

        createSampler();
        createParamsBuffer();
        createDescriptorLayout();
        createDescriptorPool();
        allocateDescriptorSets();
        createComputePipeline();

        initialized = true;
        vfLogInfo("MotionVectorPass: Initialized");
    }

    void MotionVectorPass::cleanup()
    {
        if (!initialized) return;
        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        vkDevice.destroyPipeline(computePipeline);
        vkDevice.destroyPipelineLayout(pipelineLayout);
        vkDevice.destroyDescriptorPool(descriptorPool);
        vkDevice.destroyDescriptorSetLayout(descriptorSetLayout);
        vkDevice.destroySampler(depthSampler);
        paramsBuffer.destroy(vkDevice, device.getMemoryManager());

        shader.reset();
        initialized = false;
    }

    void MotionVectorPass::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eNearest;
        samplerInfo.minFilter = vk::Filter::eNearest;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;

        depthSampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void MotionVectorPass::createParamsBuffer()
    {
        paramsBuffer.create(device.getLogicalDevice(), device.getPhysicalDevice(),
                            sizeof(MotionVectorParams), device.getMemoryManager());
    }

    void MotionVectorPass::createDescriptorLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // binding 0: depth sampler, binding 1: params UBO, binding 2: motion vector storage image
        std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};
        bindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        bindings[1] = {1, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute};
        bindings[2] = {2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute};

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
    }

    void MotionVectorPass::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 3> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eCombinedImageSampler, core::MAX_FRAMES_IN_FLIGHT};
        poolSizes[1] = {vk::DescriptorType::eUniformBuffer, core::MAX_FRAMES_IN_FLIGHT};
        poolSizes[2] = {vk::DescriptorType::eStorageImage, core::MAX_FRAMES_IN_FLIGHT};

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = core::MAX_FRAMES_IN_FLIGHT;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
    }

    void MotionVectorPass::allocateDescriptorSets()
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

    void MotionVectorPass::createComputePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/postprocess/motion_vectors_fullscreen.glsl");

        if (shader->getShaderStages().empty())
        {
            vfLogError("MotionVectorPass: Failed to compile shader: {}", shader->getLastCompilationError());
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

    void MotionVectorPass::dispatch(vk::CommandBuffer cmd,
                                     vk::ImageView depthView,
                                     vk::Image depthImage,
                                     vk::ImageView motionVectorStorageView,
                                     vk::Image motionVectorImage,
                                     const glm::mat4& invViewProjection,
                                     const glm::mat4& prevViewProjection,
                                     uint32_t width, uint32_t height,
                                     uint32_t frameIndex)
    {
        if (!initialized) return;

        uint32_t frame = frameIndex % core::MAX_FRAMES_IN_FLIGHT;
        paramsBuffer.setFrame(frame);

        // Update params UBO
        MotionVectorParams params{};
        params.invViewProjection = invViewProjection;
        params.prevViewProjection = prevViewProjection;
        params.screenParams = glm::vec4(
            static_cast<float>(width), static_cast<float>(height),
            1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
        paramsBuffer.write(&params, sizeof(params));

        // Update descriptor set for this frame
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorImageInfo depthInfo{};
        depthInfo.sampler = depthSampler;
        depthInfo.imageView = depthView;
        depthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = paramsBuffer.getBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(MotionVectorParams);

        vk::DescriptorImageInfo mvInfo{};
        mvInfo.imageView = motionVectorStorageView;
        mvInfo.imageLayout = vk::ImageLayout::eGeneral;

        std::array<vk::WriteDescriptorSet, 3> writes{};
        writes[0].dstSet = descriptorSets[frame];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[0].pImageInfo = &depthInfo;

        writes[1].dstSet = descriptorSets[frame];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[1].pBufferInfo = &bufferInfo;

        writes[2].dstSet = descriptorSets[frame];
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = vk::DescriptorType::eStorageImage;
        writes[2].pImageInfo = &mvInfo;

        vkDevice.updateDescriptorSets(writes, {});

        // Transition motion vector image to general for compute write
        vk::ImageMemoryBarrier mvBarrier{};
        mvBarrier.oldLayout = vk::ImageLayout::eUndefined;
        mvBarrier.newLayout = vk::ImageLayout::eGeneral;
        mvBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        mvBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        mvBarrier.image = motionVectorImage;
        mvBarrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        mvBarrier.srcAccessMask = {};
        mvBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, {}, mvBarrier);

        // Dispatch compute
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout,
                               0, descriptorSets[frame], {});

        uint32_t groupsX = (width + 7) / 8;
        uint32_t groupsY = (height + 7) / 8;
        cmd.dispatch(groupsX, groupsY, 1);

        // Transition motion vector image to shader read for upscaler consumption
        vk::ImageMemoryBarrier readBarrier{};
        readBarrier.oldLayout = vk::ImageLayout::eGeneral;
        readBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        readBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        readBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        readBarrier.image = motionVectorImage;
        readBarrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        readBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        readBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, {}, readBarrier);
    }
}
