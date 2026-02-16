#include "VolumetricGridManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/MemoryUtilities.hpp"
#include <cstring>
#include <array>

namespace render::volumetric
{
    VolumetricGridManager::VolumetricGridManager(core::Device& device)
        : device{device}
    {
    }

    VolumetricGridManager::~VolumetricGridManager()
    {
        if (initialized)
            cleanup();
    }

    void VolumetricGridManager::init(VolumetricQuality quality)
    {
        dims = VolumetricGridDimensions::fromQuality(quality);

        createSampler();
        createImages();
        createParamsBuffer();
        createDescriptorSetLayout();
        createDescriptorPool();
        allocateDescriptorSet();
        writeDescriptors();

        initialized = true;
    }

    void VolumetricGridManager::cleanup()
    {
        if (!initialized)
            return;

        auto& dev = device.getLogicalDevice();

        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
            descriptorSet = nullptr;
        }

        if (descriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        if (paramsBuffer)
        {
            if (paramsMapped)
            {
                dev.unmapMemory(paramsMemory);
                paramsMapped = nullptr;
            }
            core::BufferUtilities::destroyBuffer(dev, paramsBuffer, paramsMemory);
        }

        destroyImages();

        if (trilinearSampler)
        {
            dev.destroySampler(trilinearSampler);
            trilinearSampler = nullptr;
        }

        initialized = false;
    }

    void VolumetricGridManager::recreate(VolumetricQuality quality)
    {
        cleanup();
        init(quality);
    }

    void VolumetricGridManager::swapHistory()
    {
        currentHistoryIndex = 1 - currentHistoryIndex;
        writeDescriptors();
    }

    void VolumetricGridManager::updateParams(const GPUVolumetricParams& params)
    {
        if (paramsMapped)
        {
            std::memcpy(paramsMapped, &params, sizeof(GPUVolumetricParams));
        }
    }

    void VolumetricGridManager::create3DImage(vk::Image& image, vk::DeviceMemory& memory,
                                               vk::ImageView& view, vk::ImageUsageFlags usage)
    {
        auto& dev = device.getLogicalDevice();
        auto& physDev = device.getPhysicalDevice();

        vk::ImageCreateInfo imageInfo{};
        imageInfo.imageType = vk::ImageType::e3D;
        imageInfo.extent = vk::Extent3D{dims.width, dims.height, dims.depth};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = vk::Format::eR16G16B16A16Sfloat;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;
        imageInfo.usage = usage;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;

        image = dev.createImage(imageInfo);

        vk::MemoryRequirements memReqs = dev.getImageMemoryRequirements(image);
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = core::MemoryUtilities::findMemoryType(
            physDev, memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        memory = dev.allocateMemory(allocInfo);
        dev.bindImageMemory(image, memory, 0);

        core::ImageViewInfoRequest viewReq(dev, image);
        viewReq.format = vk::Format::eR16G16B16A16Sfloat;
        viewReq.imageType = vk::ImageViewType::e3D;
        viewReq.aspectFlags = vk::ImageAspectFlagBits::eColor;
        core::ImageUtilities::createImageView(viewReq, view);
    }

    void VolumetricGridManager::destroy3DImage(vk::Image& image, vk::DeviceMemory& memory, vk::ImageView& view)
    {
        auto& dev = device.getLogicalDevice();

        if (view)
        {
            dev.destroyImageView(view);
            view = nullptr;
        }
        if (image)
        {
            dev.destroyImage(image);
            image = nullptr;
        }
        if (memory)
        {
            dev.freeMemory(memory);
            memory = nullptr;
        }
    }

    void VolumetricGridManager::createImages()
    {
        // Scattering: written by injection compute, read by temporal compute
        create3DImage(scatteringImage, scatteringMemory, scatteringView,
                      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled);

        // History ping-pong: read as sampler, write as storage
        for (int i = 0; i < 2; ++i)
        {
            create3DImage(historyImages[i], historyMemory[i], historyViews[i],
                          vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled);
        }

        // Integrated output: written by ray march, read by composite fragment shader
        create3DImage(integratedImage, integratedMemory, integratedView,
                      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled);
    }

    void VolumetricGridManager::destroyImages()
    {
        destroy3DImage(scatteringImage, scatteringMemory, scatteringView);
        for (int i = 0; i < 2; ++i)
            destroy3DImage(historyImages[i], historyMemory[i], historyViews[i]);
        destroy3DImage(integratedImage, integratedMemory, integratedView);
    }

    void VolumetricGridManager::createSampler()
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

        trilinearSampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void VolumetricGridManager::createParamsBuffer()
    {
        auto& dev = device.getLogicalDevice();

        core::BufferInfoRequest bufReq(dev, device.getPhysicalDevice());
        bufReq.size = sizeof(GPUVolumetricParams);
        bufReq.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufReq.properties = vk::MemoryPropertyFlagBits::eHostVisible
                          | vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(bufReq, paramsBuffer, paramsMemory);
        paramsMapped = dev.mapMemory(paramsMemory, 0, sizeof(GPUVolumetricParams));

        GPUVolumetricParams defaultParams{};
        std::memcpy(paramsMapped, &defaultParams, sizeof(GPUVolumetricParams));
    }

    void VolumetricGridManager::createDescriptorSetLayout()
    {
        std::array<vk::DescriptorSetLayoutBinding, 5> bindings{};

        // Binding 0: Params UBO
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 1: Scattering volume (storage image, write)
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageImage;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 2: History read (combined image sampler)
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 3: History write (storage image)
        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eStorageImage;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 4: Integrated output (storage image)
        bindings[4].binding = 4;
        bindings[4].descriptorType = vk::DescriptorType::eStorageImage;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void VolumetricGridManager::createDescriptorPool()
    {
        std::array<vk::DescriptorPoolSize, 3> poolSizes{};

        poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = 1;

        poolSizes[1].type = vk::DescriptorType::eStorageImage;
        poolSizes[1].descriptorCount = 3; // scattering + history write + integrated

        poolSizes[2].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[2].descriptorCount = 1; // history read

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void VolumetricGridManager::allocateDescriptorSet()
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
    }

    void VolumetricGridManager::writeDescriptors()
    {
        auto& dev = device.getLogicalDevice();

        // Binding 0: Params UBO
        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = paramsBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(GPUVolumetricParams);

        // Binding 1: Scattering volume (storage image)
        vk::DescriptorImageInfo scatteringInfo{};
        scatteringInfo.imageView = scatteringView;
        scatteringInfo.imageLayout = vk::ImageLayout::eGeneral;

        // Binding 2: History read (previous frame)
        vk::DescriptorImageInfo historyReadInfo{};
        historyReadInfo.imageView = historyViews[1 - currentHistoryIndex]; // previous
        historyReadInfo.imageLayout = vk::ImageLayout::eGeneral;
        historyReadInfo.sampler = trilinearSampler;

        // Binding 3: History write (current frame)
        vk::DescriptorImageInfo historyWriteInfo{};
        historyWriteInfo.imageView = historyViews[currentHistoryIndex]; // current
        historyWriteInfo.imageLayout = vk::ImageLayout::eGeneral;

        // Binding 4: Integrated output
        vk::DescriptorImageInfo integratedInfo{};
        integratedInfo.imageView = integratedView;
        integratedInfo.imageLayout = vk::ImageLayout::eGeneral;

        std::array<vk::WriteDescriptorSet, 5> writes{};

        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &bufferInfo;

        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageImage;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &scatteringInfo;

        writes[2].dstSet = descriptorSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &historyReadInfo;

        writes[3].dstSet = descriptorSet;
        writes[3].dstBinding = 3;
        writes[3].descriptorType = vk::DescriptorType::eStorageImage;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo = &historyWriteInfo;

        writes[4].dstSet = descriptorSet;
        writes[4].dstBinding = 4;
        writes[4].descriptorType = vk::DescriptorType::eStorageImage;
        writes[4].descriptorCount = 1;
        writes[4].pImageInfo = &integratedInfo;

        dev.updateDescriptorSets(writes, nullptr);
    }
}
