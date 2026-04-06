#include "SSRPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"

namespace render::ssr
{
    void SSRPipeline::createDescriptorSetLayouts()
    {
        auto& dev = device.getLogicalDevice();

        // Trace Set 0: scene color sampler
        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;

            traceSet0Layout = dev.createDescriptorSetLayout(layoutInfo);
        }

        // Trace Set 1: depth + hiZ + normalRoughness + UBO
        {
            std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};
            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[2].binding = 2;
            bindings[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[2].descriptorCount = 1;
            bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[3].binding = 3;
            bindings[3].descriptorType = vk::DescriptorType::eUniformBuffer;
            bindings[3].descriptorCount = 1;
            bindings[3].stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            traceSet1Layout = dev.createDescriptorSetLayout(layoutInfo);
        }

        // Temporal Set 0: ssrRaw
        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;

            temporalSet0Layout = dev.createDescriptorSetLayout(layoutInfo);
        }

        // Temporal Set 1: history + depth + UBO
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

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            temporalSet1Layout = dev.createDescriptorSetLayout(layoutInfo);
        }

        // Denoise Set 0: ssrAccum + depth
        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            denoiseSet0Layout = dev.createDescriptorSetLayout(layoutInfo);
        }

        // Composite Set 0: ssrDenoised + depth
        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            compositeSet0Layout = dev.createDescriptorSetLayout(layoutInfo);
        }
    }

    void SSRPipeline::createDescriptorPool()
    {
        uint32_t imageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());

        // Sampler count breakdown:
        //   traceSet0: imageCount (scene color per swap image)
        //   traceSet1: 3 (depth + hiZ + normalRoughness)
        //   temporalSet0: 1 (ssrRaw)
        //   temporalSet1 x2: 2*2=4 (history + depth)
        //   denoiseHorizSet0 x2: 2*2=4 (history + depth)
        //   denoiseSet0: 2 (denoiseHoriz + depth)
        //   compositeSet0: 2 (denoised + depth)
        // Total: imageCount + 16
        // UBO count: traceSet1 + temporalSet1 x2 = 3
        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = imageCount + 16;

        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 3;

        // Sets: imageCount (traceSet0) + 1 (traceSet1) + 1 (temporalSet0) + 2 (temporalSet1)
        //       + 2 (denoiseHoriz) + 1 (denoiseSet0) + 1 (compositeSet0) = imageCount + 8
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = imageCount + 8;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void SSRPipeline::createDescriptorSets()
    {
        auto& dev = device.getLogicalDevice();
        uint32_t imageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());

        {
            std::vector<vk::DescriptorSetLayout> layouts(imageCount, traceSet0Layout);
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = imageCount;
            allocInfo.pSetLayouts = layouts.data();
            traceSet0PerImage = dev.allocateDescriptorSets(allocInfo);
        }

        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &traceSet1Layout;
            traceSet1 = dev.allocateDescriptorSets(allocInfo)[0];
        }

        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &temporalSet0Layout;
            temporalSet0 = dev.allocateDescriptorSets(allocInfo)[0];
        }

        {
            std::array<vk::DescriptorSetLayout, 2> layouts = {temporalSet1Layout, temporalSet1Layout};
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 2;
            allocInfo.pSetLayouts = layouts.data();
            auto sets = dev.allocateDescriptorSets(allocInfo);
            temporalSet1PerHistory[0] = sets[0];
            temporalSet1PerHistory[1] = sets[1];
        }

        {
            std::array<vk::DescriptorSetLayout, 2> layouts = {denoiseSet0Layout, denoiseSet0Layout};
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 2;
            allocInfo.pSetLayouts = layouts.data();
            auto sets = dev.allocateDescriptorSets(allocInfo);
            denoiseHorizSet0PerHistory[0] = sets[0];
            denoiseHorizSet0PerHistory[1] = sets[1];
        }

        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &denoiseSet0Layout;
            denoiseSet0 = dev.allocateDescriptorSets(allocInfo)[0];
        }

        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &compositeSet0Layout;
            compositeSet0 = dev.allocateDescriptorSets(allocInfo)[0];
        }
    }

    void SSRPipeline::updateDescriptorSets()
    {
        auto& dev = device.getLogicalDevice();
        uint32_t imageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());

        vk::DescriptorImageInfo depthImageInfo{};
        depthImageInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        depthImageInfo.imageView = depthOnlyImageView;
        depthImageInfo.sampler = sampler;

        vk::DescriptorBufferInfo uboInfo{};
        uboInfo.buffer = paramsBuffer;
        uboInfo.offset = 0;
        uboInfo.range = sizeof(SSRParamsUBO);

        std::vector<vk::WriteDescriptorSet> writes;

        // Trace Set 0: scene color per swap image
        std::vector<vk::DescriptorImageInfo> sceneColorInfos(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            sceneColorInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            sceneColorInfos[i].imageView = offscreenResources.colorImages[i].colorImageView;
            sceneColorInfos[i].sampler = sampler;

            vk::WriteDescriptorSet w{};
            w.dstSet = traceSet0PerImage[i];
            w.dstBinding = 0;
            w.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w.descriptorCount = 1;
            w.pImageInfo = &sceneColorInfos[i];
            writes.push_back(w);
        }

        // Trace Set 1: depth + hiZ + normalRoughness + UBO
        vk::DescriptorImageInfo hiZImageInfo{};
        hiZImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        hiZImageInfo.imageView = hiZImageView ? hiZImageView : depthOnlyImageView; // fallback
        hiZImageInfo.sampler = hiZSampler ? hiZSampler : sampler;

        vk::DescriptorImageInfo normalRoughnessInfo{};
        normalRoughnessInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        normalRoughnessInfo.imageView = normalRoughnessImageView ? normalRoughnessImageView : depthOnlyImageView; // fallback
        normalRoughnessInfo.sampler = sampler;

        {
            vk::WriteDescriptorSet w0{};
            w0.dstSet = traceSet1;
            w0.dstBinding = 0;
            w0.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w0.descriptorCount = 1;
            w0.pImageInfo = &depthImageInfo;
            writes.push_back(w0);

            vk::WriteDescriptorSet w1{};
            w1.dstSet = traceSet1;
            w1.dstBinding = 1;
            w1.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w1.descriptorCount = 1;
            w1.pImageInfo = &hiZImageInfo;
            writes.push_back(w1);

            vk::WriteDescriptorSet w2{};
            w2.dstSet = traceSet1;
            w2.dstBinding = 2;
            w2.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w2.descriptorCount = 1;
            w2.pImageInfo = &normalRoughnessInfo;
            writes.push_back(w2);

            vk::WriteDescriptorSet w3{};
            w3.dstSet = traceSet1;
            w3.dstBinding = 3;
            w3.descriptorType = vk::DescriptorType::eUniformBuffer;
            w3.descriptorCount = 1;
            w3.pBufferInfo = &uboInfo;
            writes.push_back(w3);
        }

        // Temporal Set 0: ssrRaw
        vk::DescriptorImageInfo ssrRawInfo{};
        ssrRawInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        ssrRawInfo.imageView = ssrRawImageView;
        ssrRawInfo.sampler = sampler;
        {
            vk::WriteDescriptorSet w{};
            w.dstSet = temporalSet0;
            w.dstBinding = 0;
            w.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w.descriptorCount = 1;
            w.pImageInfo = &ssrRawInfo;
            writes.push_back(w);
        }

        // Temporal Set 1: history + depth + UBO (per history buffer)
        std::array<vk::DescriptorImageInfo, 2> historyInfos{};
        for (uint32_t i = 0; i < 2; ++i)
        {
            historyInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            historyInfos[i].imageView = ssrHistoryImageViews[i];
            historyInfos[i].sampler = sampler;

            vk::WriteDescriptorSet wHist{};
            wHist.dstSet = temporalSet1PerHistory[i];
            wHist.dstBinding = 0;
            wHist.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            wHist.descriptorCount = 1;
            wHist.pImageInfo = &historyInfos[i];
            writes.push_back(wHist);

            vk::WriteDescriptorSet wDepth{};
            wDepth.dstSet = temporalSet1PerHistory[i];
            wDepth.dstBinding = 1;
            wDepth.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            wDepth.descriptorCount = 1;
            wDepth.pImageInfo = &depthImageInfo;
            writes.push_back(wDepth);

            vk::WriteDescriptorSet wUbo{};
            wUbo.dstSet = temporalSet1PerHistory[i];
            wUbo.dstBinding = 2;
            wUbo.descriptorType = vk::DescriptorType::eUniformBuffer;
            wUbo.descriptorCount = 1;
            wUbo.pBufferInfo = &uboInfo;
            writes.push_back(wUbo);
        }

        // Denoise Set 0 per history: reads ssrHistory[i] + depth
        std::array<vk::DescriptorImageInfo, 2> denoiseHistoryInfos{};
        for (uint32_t i = 0; i < 2; ++i)
        {
            denoiseHistoryInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            denoiseHistoryInfos[i].imageView = ssrHistoryImageViews[i];
            denoiseHistoryInfos[i].sampler = sampler;

            vk::WriteDescriptorSet w0{};
            w0.dstSet = denoiseHorizSet0PerHistory[i];
            w0.dstBinding = 0;
            w0.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w0.descriptorCount = 1;
            w0.pImageInfo = &denoiseHistoryInfos[i];
            writes.push_back(w0);

            vk::WriteDescriptorSet w1{};
            w1.dstSet = denoiseHorizSet0PerHistory[i];
            w1.dstBinding = 1;
            w1.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w1.descriptorCount = 1;
            w1.pImageInfo = &depthImageInfo;
            writes.push_back(w1);
        }

        // Denoise Set 0: reads denoiseHoriz + depth
        vk::DescriptorImageInfo ssrDenoiseHorizInfo{};
        ssrDenoiseHorizInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        ssrDenoiseHorizInfo.imageView = ssrDenoiseHorizImageView;
        ssrDenoiseHorizInfo.sampler = sampler;
        {
            vk::WriteDescriptorSet w0{};
            w0.dstSet = denoiseSet0;
            w0.dstBinding = 0;
            w0.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w0.descriptorCount = 1;
            w0.pImageInfo = &ssrDenoiseHorizInfo;
            writes.push_back(w0);

            vk::WriteDescriptorSet w1{};
            w1.dstSet = denoiseSet0;
            w1.dstBinding = 1;
            w1.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w1.descriptorCount = 1;
            w1.pImageInfo = &depthImageInfo;
            writes.push_back(w1);
        }

        // Composite Set 0: ssrDenoised + depth
        vk::DescriptorImageInfo ssrDenoisedInfo{};
        ssrDenoisedInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        ssrDenoisedInfo.imageView = ssrDenoisedImageView;
        ssrDenoisedInfo.sampler = sampler;
        {
            vk::WriteDescriptorSet w0{};
            w0.dstSet = compositeSet0;
            w0.dstBinding = 0;
            w0.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w0.descriptorCount = 1;
            w0.pImageInfo = &ssrDenoisedInfo;
            writes.push_back(w0);

            vk::WriteDescriptorSet w1{};
            w1.dstSet = compositeSet0;
            w1.dstBinding = 1;
            w1.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w1.descriptorCount = 1;
            w1.pImageInfo = &depthImageInfo;
            writes.push_back(w1);
        }

        dev.updateDescriptorSets(writes, nullptr);
    }

}
