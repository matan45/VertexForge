#include "OceanFFTResources.hpp"
#include "../../core/Device.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"

namespace render::water
{
    OceanFFTResources::OceanFFTResources(core::Device& device)
        : device(device)
    {
    }

    OceanFFTResources::~OceanFFTResources()
    {
        cleanup();
    }

    void OceanFFTResources::init(uint32_t resolution)
    {
        createTextures(resolution);
        createSampler();
        createDescriptorLayouts();
        createDescriptorPool();
        allocateDescriptorSets();
        updateDescriptorSets();
        transitionImagesInitial();
    }

    void OceanFFTResources::cleanup()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        if (descriptorPool)
        {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (spectrumDescLayout)     { vkDevice.destroyDescriptorSetLayout(spectrumDescLayout);     spectrumDescLayout = nullptr; }
        if (timeEvolveDescLayout)   { vkDevice.destroyDescriptorSetLayout(timeEvolveDescLayout);   timeEvolveDescLayout = nullptr; }
        if (fftDescLayout)          { vkDevice.destroyDescriptorSetLayout(fftDescLayout);          fftDescLayout = nullptr; }
        if (mergeDescLayout)        { vkDevice.destroyDescriptorSetLayout(mergeDescLayout);        mergeDescLayout = nullptr; }
        if (oceanTextureDescLayout) { vkDevice.destroyDescriptorSetLayout(oceanTextureDescLayout); oceanTextureDescLayout = nullptr; }

        if (outputSampler) { vkDevice.destroySampler(outputSampler); outputSampler = nullptr; }

        destroyTextures();
    }

    void OceanFFTResources::createTextures(uint32_t N)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // h0 Spectrum: RGBA32F
        {
            core::ImageInfoRequest req(vkDevice, device.getPhysicalDevice(),
                N, N, 1, 1,
                vk::Format::eR32G32B32A32Sfloat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            core::ImageUtilities::createImage(req, h0Image, h0Memory);

            core::ImageViewInfoRequest viewReq(vkDevice, h0Image,
                vk::Format::eR32G32B32A32Sfloat,
                vk::ImageAspectFlagBits::eColor, vk::ImageViewType::e2D);
            core::ImageUtilities::createImageView(viewReq, h0View);
        }

        // Field textures: 3 fields x 2 ping-pong = 6 textures (RG32F)
        for (int f = 0; f < 3; ++f)
        {
            for (int p = 0; p < 2; ++p)
            {
                core::ImageInfoRequest req(vkDevice, device.getPhysicalDevice(),
                    N, N, 1, 1,
                    vk::Format::eR32G32Sfloat,
                    vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eStorage,
                    vk::MemoryPropertyFlagBits::eDeviceLocal);
                core::ImageUtilities::createImage(req, fields[f].images[p], fields[f].memory[p]);

                core::ImageViewInfoRequest viewReq(vkDevice, fields[f].images[p],
                    vk::Format::eR32G32Sfloat,
                    vk::ImageAspectFlagBits::eColor, vk::ImageViewType::e2D);
                core::ImageUtilities::createImageView(viewReq, fields[f].views[p]);
            }
        }

        // Displacement: RGBA16F (storage + sampled + transfer src for CPU readback)
        {
            core::ImageInfoRequest req(vkDevice, device.getPhysicalDevice(),
                N, N, 1, 1,
                vk::Format::eR16G16B16A16Sfloat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            core::ImageUtilities::createImage(req, displacementImage, displacementMemory);

            core::ImageViewInfoRequest viewReq(vkDevice, displacementImage,
                vk::Format::eR16G16B16A16Sfloat,
                vk::ImageAspectFlagBits::eColor, vk::ImageViewType::e2D);
            core::ImageUtilities::createImageView(viewReq, displacementView);
        }

        // Normal: RGBA16F (storage + sampled)
        {
            core::ImageInfoRequest req(vkDevice, device.getPhysicalDevice(),
                N, N, 1, 1,
                vk::Format::eR16G16B16A16Sfloat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            core::ImageUtilities::createImage(req, normalImage, normalMemory);

            core::ImageViewInfoRequest viewReq(vkDevice, normalImage,
                vk::Format::eR16G16B16A16Sfloat,
                vk::ImageAspectFlagBits::eColor, vk::ImageViewType::e2D);
            core::ImageUtilities::createImageView(viewReq, normalView);
        }
    }

    void OceanFFTResources::createSampler()
    {
        vk::SamplerCreateInfo info{};
        info.magFilter = vk::Filter::eLinear;
        info.minFilter = vk::Filter::eLinear;
        info.addressModeU = vk::SamplerAddressMode::eRepeat;
        info.addressModeV = vk::SamplerAddressMode::eRepeat;
        info.addressModeW = vk::SamplerAddressMode::eRepeat;
        info.anisotropyEnable = VK_FALSE;
        info.maxAnisotropy = 1.0f;
        info.mipmapMode = vk::SamplerMipmapMode::eLinear;

        outputSampler = device.getLogicalDevice().createSampler(info);
    }

    void OceanFFTResources::createDescriptorLayouts()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Spectrum init: 1 storage image (writeonly)
        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eStorageImage;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eCompute;

            vk::DescriptorSetLayoutCreateInfo info{};
            info.bindingCount = 1;
            info.pBindings = &binding;
            spectrumDescLayout = vkDevice.createDescriptorSetLayout(info);
        }

        // Time evolve: 1 readonly + 3 writeonly storage images
        {
            std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};
            for (uint32_t i = 0; i < 4; ++i)
            {
                bindings[i].binding = i;
                bindings[i].descriptorType = vk::DescriptorType::eStorageImage;
                bindings[i].descriptorCount = 1;
                bindings[i].stageFlags = vk::ShaderStageFlagBits::eCompute;
            }

            vk::DescriptorSetLayoutCreateInfo info{};
            info.bindingCount = static_cast<uint32_t>(bindings.size());
            info.pBindings = bindings.data();
            timeEvolveDescLayout = vkDevice.createDescriptorSetLayout(info);
        }

        // FFT: 2 storage images (input + output)
        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            for (uint32_t i = 0; i < 2; ++i)
            {
                bindings[i].binding = i;
                bindings[i].descriptorType = vk::DescriptorType::eStorageImage;
                bindings[i].descriptorCount = 1;
                bindings[i].stageFlags = vk::ShaderStageFlagBits::eCompute;
            }

            vk::DescriptorSetLayoutCreateInfo info{};
            info.bindingCount = static_cast<uint32_t>(bindings.size());
            info.pBindings = bindings.data();
            fftDescLayout = vkDevice.createDescriptorSetLayout(info);
        }

        // Merge: 3 readonly + 2 writeonly storage images
        {
            std::array<vk::DescriptorSetLayoutBinding, 5> bindings{};
            for (uint32_t i = 0; i < 5; ++i)
            {
                bindings[i].binding = i;
                bindings[i].descriptorType = vk::DescriptorType::eStorageImage;
                bindings[i].descriptorCount = 1;
                bindings[i].stageFlags = vk::ShaderStageFlagBits::eCompute;
            }

            vk::DescriptorSetLayoutCreateInfo info{};
            info.bindingCount = static_cast<uint32_t>(bindings.size());
            info.pBindings = bindings.data();
            mergeDescLayout = vkDevice.createDescriptorSetLayout(info);
        }

        // Ocean texture output: 2 combined image samplers for graphics pipeline
        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo info{};
            info.bindingCount = static_cast<uint32_t>(bindings.size());
            info.pBindings = bindings.data();
            oceanTextureDescLayout = vkDevice.createDescriptorSetLayout(info);
        }
    }

    void OceanFFTResources::createDescriptorPool()
    {
        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eStorageImage;
        poolSizes[0].descriptorCount = 22;
        poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = 2;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 10;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void OceanFFTResources::allocateDescriptorSets()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        auto allocSet = [&](vk::DescriptorSetLayout layout) -> vk::DescriptorSet
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &layout;
            return vkDevice.allocateDescriptorSets(allocInfo)[0];
        };

        spectrumDescSet = allocSet(spectrumDescLayout);
        timeEvolveDescSet = allocSet(timeEvolveDescLayout);

        for (int i = 0; i < 6; ++i)
            fftDescSets[i] = allocSet(fftDescLayout);

        mergeDescSet = allocSet(mergeDescLayout);
        oceanTextureDescSet = allocSet(oceanTextureDescLayout);
    }

    void OceanFFTResources::updateDescriptorSets()
    {
        vk::Device vkDevice = device.getLogicalDevice();
        std::vector<vk::WriteDescriptorSet> writes;
        std::vector<vk::DescriptorImageInfo> imageInfos;
        imageInfos.reserve(32);

        auto addStorageImageWrite = [&](vk::DescriptorSet set, uint32_t binding, vk::ImageView view)
        {
            imageInfos.push_back({nullptr, view, vk::ImageLayout::eGeneral});

            vk::WriteDescriptorSet write{};
            write.dstSet = set;
            write.dstBinding = binding;
            write.descriptorCount = 1;
            write.descriptorType = vk::DescriptorType::eStorageImage;
            write.pImageInfo = &imageInfos.back();
            writes.push_back(write);
        };

        auto addSampledImageWrite = [&](vk::DescriptorSet set, uint32_t binding, vk::ImageView view)
        {
            imageInfos.push_back({outputSampler, view, vk::ImageLayout::eShaderReadOnlyOptimal});

            vk::WriteDescriptorSet write{};
            write.dstSet = set;
            write.dstBinding = binding;
            write.descriptorCount = 1;
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            write.pImageInfo = &imageInfos.back();
            writes.push_back(write);
        };

        // Spectrum init: binding 0 = h0
        addStorageImageWrite(spectrumDescSet, 0, h0View);

        // Time evolve: binding 0 = h0 (read), 1-3 = field[0..2] ping-pong 0 (write)
        addStorageImageWrite(timeEvolveDescSet, 0, h0View);
        for (int f = 0; f < 3; ++f)
            addStorageImageWrite(timeEvolveDescSet, f + 1, fields[f].views[0]);

        // FFT: 3 fields x 2 directions
        for (int f = 0; f < 3; ++f)
        {
            addStorageImageWrite(fftDescSets[f * 2 + 0], 0, fields[f].views[0]);
            addStorageImageWrite(fftDescSets[f * 2 + 0], 1, fields[f].views[1]);

            addStorageImageWrite(fftDescSets[f * 2 + 1], 0, fields[f].views[1]);
            addStorageImageWrite(fftDescSets[f * 2 + 1], 1, fields[f].views[0]);
        }

        // Merge: bindings 0-2 = field results (ping-pong 0), 3 = displacement, 4 = normal
        for (int f = 0; f < 3; ++f)
            addStorageImageWrite(mergeDescSet, f, fields[f].views[0]);
        addStorageImageWrite(mergeDescSet, 3, displacementView);
        addStorageImageWrite(mergeDescSet, 4, normalView);

        // Ocean textures for graphics sampling
        addSampledImageWrite(oceanTextureDescSet, 0, displacementView);
        addSampledImageWrite(oceanTextureDescSet, 1, normalView);

        vkDevice.updateDescriptorSets(writes, nullptr);
    }

    void OceanFFTResources::transitionImagesInitial()
    {
        auto cmd = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), device.getStagingCommandPool());

        auto transitionToGeneral = [&](vk::Image image)
        {
            core::ImageUtilities::transitionImageLayout(
                cmd.get(), image,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
                vk::ImageAspectFlagBits::eColor);
        };

        transitionToGeneral(h0Image);

        for (int f = 0; f < 3; ++f)
        {
            transitionToGeneral(fields[f].images[0]);
            transitionToGeneral(fields[f].images[1]);
        }

        transitionToGeneral(displacementImage);
        transitionToGeneral(normalImage);

        core::Utilities::endSingleTimeCommands(device, cmd);
    }

    void OceanFFTResources::destroyTextures()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        auto destroyImage = [&](vk::Image& img, vk::DeviceMemory& mem, vk::ImageView& view)
        {
            if (view) { vkDevice.destroyImageView(view); view = nullptr; }
            if (img)  { vkDevice.destroyImage(img); img = nullptr; }
            if (mem)  { vkDevice.freeMemory(mem); mem = nullptr; }
        };

        destroyImage(h0Image, h0Memory, h0View);

        for (int f = 0; f < 3; ++f)
        {
            destroyImage(fields[f].images[0], fields[f].memory[0], fields[f].views[0]);
            destroyImage(fields[f].images[1], fields[f].memory[1], fields[f].views[1]);
        }

        destroyImage(displacementImage, displacementMemory, displacementView);
        destroyImage(normalImage, normalMemory, normalView);
    }
}
