#include "ProbeStorageBuffer.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"
#include <cstring>

namespace render::gi
{
    static constexpr uint32_t MAX_CASCADES = 8;

    ProbeStorageBuffer::ProbeStorageBuffer(core::Device& device)
        : device(device)
    {
    }

    ProbeStorageBuffer::~ProbeStorageBuffer()
    {
        cleanup();
    }

    void ProbeStorageBuffer::init(uint32_t maxProbes, uint32_t maxCascades)
    {
        if (initialized)
        {
            cleanup();
        }

        probeCount = maxProbes;
        cascadeCount = maxCascades;
        currentReadBuffer = 0;

        createBuffers();
        createDescriptorLayouts();
        createDescriptorPool();
        allocateDescriptorSets();
        updateDescriptors();

        initialized = true;
        vfLogInfo("ProbeStorageBuffer: Initialized with {} probes, {} cascades ({} KB)",
                  probeCount, cascadeCount,
                  (probeCount * sizeof(ProbeData) * 2 + MAX_CASCADES * sizeof(GPUCascadeInfo)) / 1024);
    }

    void ProbeStorageBuffer::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (descriptorPool)
        {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (probeDataLayout)
        {
            vkDevice.destroyDescriptorSetLayout(probeDataLayout);
            probeDataLayout = nullptr;
        }
        if (cascadeInfoLayout)
        {
            vkDevice.destroyDescriptorSetLayout(cascadeInfoLayout);
            cascadeInfoLayout = nullptr;
        }
        if (samplingLayout)
        {
            vkDevice.destroyDescriptorSetLayout(samplingLayout);
            samplingLayout = nullptr;
        }

        destroyBuffers();
        initialized = false;
    }

    void ProbeStorageBuffer::createBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        vk::DeviceSize probeBufferSize = probeCount * sizeof(ProbeData);

        // Probe buffer A (device local)
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = probeBufferSize;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, probeBufferA, probeMemoryA);
        }

        // Probe buffer B (device local)
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = probeBufferSize;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, probeBufferB, probeMemoryB);
        }

        // Staging buffer for probe data
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = probeBufferSize;
            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, probeStagingBuffer, probeStagingMemory);
            probeStagingMapped = logicalDevice.mapMemory(probeStagingMemory, 0, probeBufferSize, vk::MemoryMapFlags{});

            // Zero-initialize
            std::memset(probeStagingMapped, 0, probeBufferSize);
        }

        // Cascade info UBO (16 bytes header + cascade data)
        {
            vk::DeviceSize cascadeBufferSize = 16 + MAX_CASCADES * sizeof(GPUCascadeInfo);

            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = cascadeBufferSize;
            request.usage = vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, cascadeInfoBuffer, cascadeInfoMemory);
            cascadeInfoMapped = logicalDevice.mapMemory(cascadeInfoMemory, 0, cascadeBufferSize, vk::MemoryMapFlags{});

            std::memset(cascadeInfoMapped, 0, static_cast<size_t>(cascadeBufferSize));
        }
    }

    void ProbeStorageBuffer::destroyBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (probeStagingMapped)
        {
            logicalDevice.unmapMemory(probeStagingMemory);
            probeStagingMapped = nullptr;
        }
        if (cascadeInfoMapped)
        {
            logicalDevice.unmapMemory(cascadeInfoMemory);
            cascadeInfoMapped = nullptr;
        }

        core::BufferUtilities::destroyBuffer(logicalDevice, probeBufferA, probeMemoryA);
        core::BufferUtilities::destroyBuffer(logicalDevice, probeBufferB, probeMemoryB);
        core::BufferUtilities::destroyBuffer(logicalDevice, probeStagingBuffer, probeStagingMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, cascadeInfoBuffer, cascadeInfoMemory);
    }

    void ProbeStorageBuffer::createDescriptorLayouts()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Probe data layout: binding 0 = read SSBO, binding 1 = write SSBO
        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eFragment;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();
            probeDataLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
        }

        // Cascade info layout: binding 0 = UBO
        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eUniformBuffer;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;
            cascadeInfoLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
        }

        // Sampling layout for fragment shaders: binding 0 = probe read SSBO, binding 1 = cascade SSBO
        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();
            samplingLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
        }
    }

    void ProbeStorageBuffer::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[0].descriptorCount = 8; // 2 per probe set x 2 + 2 per sampling set x 2
        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 5; // 2 probe sets + 1 cascade + 2 sampling sets
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
    }

    void ProbeStorageBuffer::allocateDescriptorSets()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Probe data set A (reads from A, writes to B)
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &probeDataLayout;
            auto sets = vkDevice.allocateDescriptorSets(allocInfo);
            probeDataDescSetA = sets[0];
        }

        // Probe data set B (reads from B, writes to A)
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &probeDataLayout;
            auto sets = vkDevice.allocateDescriptorSets(allocInfo);
            probeDataDescSetB = sets[0];
        }

        // Cascade info set
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &cascadeInfoLayout;
            auto sets = vkDevice.allocateDescriptorSets(allocInfo);
            cascadeInfoDescSet = sets[0];
        }

        // Sampling set A (reads from probe A + cascade)
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &samplingLayout;
            auto sets = vkDevice.allocateDescriptorSets(allocInfo);
            samplingDescSetA = sets[0];
        }

        // Sampling set B (reads from probe B + cascade)
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &samplingLayout;
            auto sets = vkDevice.allocateDescriptorSets(allocInfo);
            samplingDescSetB = sets[0];
        }
    }

    void ProbeStorageBuffer::updateDescriptors()
    {
        vk::Device vkDevice = device.getLogicalDevice();
        vk::DeviceSize probeBufferSize = probeCount * sizeof(ProbeData);

        // Set A: read=A, write=B
        {
            std::array<vk::DescriptorBufferInfo, 2> bufferInfos{};
            bufferInfos[0] = vk::DescriptorBufferInfo(probeBufferA, 0, probeBufferSize);
            bufferInfos[1] = vk::DescriptorBufferInfo(probeBufferB, 0, probeBufferSize);

            std::array<vk::WriteDescriptorSet, 2> writes{};
            for (uint32_t i = 0; i < 2; ++i)
            {
                writes[i].dstSet = probeDataDescSetA;
                writes[i].dstBinding = i;
                writes[i].descriptorType = vk::DescriptorType::eStorageBuffer;
                writes[i].descriptorCount = 1;
                writes[i].pBufferInfo = &bufferInfos[i];
            }
            vkDevice.updateDescriptorSets(writes, {});
        }

        // Set B: read=B, write=A
        {
            std::array<vk::DescriptorBufferInfo, 2> bufferInfos{};
            bufferInfos[0] = vk::DescriptorBufferInfo(probeBufferB, 0, probeBufferSize);
            bufferInfos[1] = vk::DescriptorBufferInfo(probeBufferA, 0, probeBufferSize);

            std::array<vk::WriteDescriptorSet, 2> writes{};
            for (uint32_t i = 0; i < 2; ++i)
            {
                writes[i].dstSet = probeDataDescSetB;
                writes[i].dstBinding = i;
                writes[i].descriptorType = vk::DescriptorType::eStorageBuffer;
                writes[i].descriptorCount = 1;
                writes[i].pBufferInfo = &bufferInfos[i];
            }
            vkDevice.updateDescriptorSets(writes, {});
        }

        // Cascade info
        {
            vk::DescriptorBufferInfo bufferInfo(cascadeInfoBuffer, 0, 16 + MAX_CASCADES * sizeof(GPUCascadeInfo));

            vk::WriteDescriptorSet write{};
            write.dstSet = cascadeInfoDescSet;
            write.dstBinding = 0;
            write.descriptorType = vk::DescriptorType::eUniformBuffer;
            write.descriptorCount = 1;
            write.pBufferInfo = &bufferInfo;
            vkDevice.updateDescriptorSets(1, &write, 0, nullptr);
        }

        // Sampling set A: probe read from A + cascade info as SSBO
        {
            vk::DescriptorBufferInfo probeInfo(probeBufferA, 0, probeBufferSize);
            vk::DescriptorBufferInfo cascadeInfo(cascadeInfoBuffer, 0, 16 + MAX_CASCADES * sizeof(GPUCascadeInfo));

            std::array<vk::WriteDescriptorSet, 2> writes{};
            writes[0].dstSet = samplingDescSetA;
            writes[0].dstBinding = 0;
            writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &probeInfo;

            writes[1].dstSet = samplingDescSetA;
            writes[1].dstBinding = 1;
            writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[1].descriptorCount = 1;
            writes[1].pBufferInfo = &cascadeInfo;

            vkDevice.updateDescriptorSets(writes, {});
        }

        // Sampling set B: probe read from B + cascade info as SSBO
        {
            vk::DescriptorBufferInfo probeInfo(probeBufferB, 0, probeBufferSize);
            vk::DescriptorBufferInfo cascadeInfo(cascadeInfoBuffer, 0, 16 + MAX_CASCADES * sizeof(GPUCascadeInfo));

            std::array<vk::WriteDescriptorSet, 2> writes{};
            writes[0].dstSet = samplingDescSetB;
            writes[0].dstBinding = 0;
            writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &probeInfo;

            writes[1].dstSet = samplingDescSetB;
            writes[1].dstBinding = 1;
            writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[1].descriptorCount = 1;
            writes[1].pBufferInfo = &cascadeInfo;

            vkDevice.updateDescriptorSets(writes, {});
        }
    }

    void ProbeStorageBuffer::uploadCascadeInfo(const std::vector<CascadeLevel>& cascades)
    {
        if (!cascadeInfoMapped)
        {
            return;
        }

        // Header: [cascadeCount, pad, pad, pad]
        auto* header = static_cast<uint32_t*>(cascadeInfoMapped);
        header[0] = static_cast<uint32_t>(std::min(cascades.size(), static_cast<size_t>(MAX_CASCADES)));
        header[1] = 0;
        header[2] = 0;
        header[3] = 0;

        // Cascade data follows header (16 bytes offset)
        auto* gpuCascades = reinterpret_cast<GPUCascadeInfo*>(header + 4);
        for (uint32_t i = 0; i < cascades.size() && i < MAX_CASCADES; ++i)
        {
            gpuCascades[i].gridOriginSpacing = glm::vec4(cascades[i].gridOrigin, cascades[i].spacing);
            gpuCascades[i].gridDimsOffset = glm::ivec4(8, 4, 8, static_cast<int>(cascades[i].probeOffset));
        }
    }

    void ProbeStorageBuffer::uploadToGPU(vk::CommandBuffer cmd)
    {
        if (!initialized)
        {
            return;
        }

        vk::DeviceSize probeBufferSize = probeCount * sizeof(ProbeData);

        // Copy staging to both probe buffers (initial zero-fill)
        vk::BufferCopy copyRegion{0, 0, probeBufferSize};
        cmd.copyBuffer(probeStagingBuffer, probeBufferA, copyRegion);
        cmd.copyBuffer(probeStagingBuffer, probeBufferB, copyRegion);
    }

    void ProbeStorageBuffer::swapBuffers()
    {
        currentReadBuffer = 1 - currentReadBuffer;
    }
}
