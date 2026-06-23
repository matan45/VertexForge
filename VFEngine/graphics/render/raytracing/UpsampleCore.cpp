#include "UpsampleCore.hpp"
#include "RTShadowSamplers.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"
#include <array>

namespace render::raytracing
{
    UpsampleCore::UpsampleCore(core::Device& device)
        : device(device)
    {
    }

    UpsampleCore::~UpsampleCore()
    {
        cleanup();
    }

    bool UpsampleCore::init(uint32_t computeSetCount)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Samplers: nearest guide (depth/normal/half-mask) + linear denoised-mask output.
        guideSampler = createGuideSampler(vkDevice);
        outputSampler = createDenoisedMaskSampler(vkDevice);

        // Compute set: half mask (sampler) + depth (sampler) + normal (sampler) + output (storage).
        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};
        bindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        bindings[1] = {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        bindings[2] = {2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        bindings[3] = {3, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute};

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        computeLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        // Compute pool sized for computeSetCount sets (3 samplers + 1 storage each).
        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eCombinedImageSampler, 3 * computeSetCount};
        poolSizes[1] = {vk::DescriptorType::eStorageImage, 1 * computeSetCount};

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = computeSetCount;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        computePool = vkDevice.createDescriptorPool(poolInfo);

        // Compute pipeline (shared 2D shader; the layered pipeline binds one e2D layer per dispatch).
        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/shadow/rt_shadow_upsample.glsl");
        if (shader->getShaderStages().empty())
        {
            vfLogError("UpsampleCore: Failed to compile shader: {}", shader->getLastCompilationError());
            // Nothing past the pipeline layout was built yet — tear down the rest and stay unbuilt.
            vkDevice.destroyDescriptorPool(computePool);
            vkDevice.destroyDescriptorSetLayout(computeLayout);
            vkDevice.destroySampler(outputSampler);
            vkDevice.destroySampler(guideSampler);
            shader.reset();
            return false;
        }

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushRange.offset = 0;
        pushRange.size = sizeof(UpsamplePushConstants);

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &computeLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        pipelineLayout = vkDevice.createPipelineLayout(pipelineLayoutInfo);

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = shader->getShaderStages()[0];
        pipelineInfo.layout = pipelineLayout;
        computePipeline = core::PipelineUtilities::createComputePipeline(vkDevice, pipelineInfo);

        if (!computePipeline)
        {
            // Pipeline creation failed — tear down what we built and stay unbuilt. pipelineLayout was
            // created before the pipeline, so destroy it here too.
            vkDevice.destroyPipelineLayout(pipelineLayout);
            vkDevice.destroyDescriptorPool(computePool);
            vkDevice.destroyDescriptorSetLayout(computeLayout);
            vkDevice.destroySampler(outputSampler);
            vkDevice.destroySampler(guideSampler);
            shader.reset();
            return false;
        }

        built = true;
        return true;
    }

    void UpsampleCore::cleanup()
    {
        if (!built) return;
        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        vkDevice.destroyPipeline(computePipeline);
        vkDevice.destroyPipelineLayout(pipelineLayout);
        vkDevice.destroyDescriptorPool(computePool);
        vkDevice.destroyDescriptorSetLayout(computeLayout);
        vkDevice.destroySampler(outputSampler);
        vkDevice.destroySampler(guideSampler);

        shader.reset();
        built = false;
    }
}
