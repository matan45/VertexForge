#include "MaterialParameterBufferCache.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "material/MaterialTypes.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cstring>
#include <span>

namespace render::mesh
{
    namespace
    {
        bool sameLayout(const material::MaterialParameterSet& a, const material::MaterialParameterSet& b)
        {
            if (a.uniformBlockSize != b.uniformBlockSize) return false;
            if (a.values.size() != b.values.size()) return false;
            for (size_t i = 0; i < a.values.size(); ++i)
            {
                if (a.values[i].glslName != b.values[i].glslName ||
                    a.values[i].byteOffset != b.values[i].byteOffset ||
                    a.values[i].type != b.values[i].type)
                {
                    return false;
                }
            }
            return true;
        }
    }

    MaterialParameterBufferCache::MaterialParameterBufferCache(core::Device& device)
        : device(device)
    {
    }

    MaterialParameterBufferCache::~MaterialParameterBufferCache()
    {
        cleanUp();
    }

    void MaterialParameterBufferCache::initLayout()
    {
        if (descriptorSetLayout) return;

        vk::DescriptorSetLayoutBinding binding{};
        binding.binding = material::PARAMETER_DESCRIPTOR_BINDING;
        binding.descriptorType = vk::DescriptorType::eUniformBufferDynamic;
        binding.descriptorCount = 1;
        binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    uint32_t MaterialParameterBufferCache::alignBlockSize(uint32_t blockSize) const
    {
        const auto limits = device.getPhysicalDevice().getProperties().limits;
        uint32_t alignment = std::max<uint32_t>(
            16, static_cast<uint32_t>(limits.minUniformBufferOffsetAlignment));
        return (blockSize + alignment - 1) & ~(alignment - 1);
    }

    MaterialParameterBufferCache::Entry* MaterialParameterBufferCache::getOrCreateEntry(
        const std::string& materialPath, const material::MaterialData& materialData)
    {
        material::MaterialParameterSet paramSet = material::collectParameters(materialData.graph);
        if (!paramSet.hasValueParameters())
        {
            auto stale = entries.find(materialPath);
            if (stale != entries.end())
            {
                device.getLogicalDevice().waitIdle();
                destroyEntry(stale->second);
                entries.erase(stale);
            }
            return nullptr;
        }

        auto it = entries.find(materialPath);
        if (it != entries.end())
        {
            if (sameLayout(it->second.paramSet, paramSet))
            {
                // Same block layout — refresh defaults so live value edits flow through
                it->second.paramSet = std::move(paramSet);
                return &it->second;
            }
            device.getLogicalDevice().waitIdle();
            destroyEntry(it->second);
            entries.erase(it);
        }

        if (entries.size() >= MAX_PARAMETER_DESCRIPTOR_SETS)
        {
            vfLogWarning("Material parameter buffer cache full ({} entries); skipping {}",
                         entries.size(), materialPath);
            return nullptr;
        }

        if (!descriptorPool)
        {
            vk::DescriptorPoolSize poolSize{};
            poolSize.type = vk::DescriptorType::eUniformBufferDynamic;
            poolSize.descriptorCount = MAX_PARAMETER_DESCRIPTOR_SETS;

            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
            poolInfo.poolSizeCount = 1;
            poolInfo.pPoolSizes = &poolSize;
            poolInfo.maxSets = MAX_PARAMETER_DESCRIPTOR_SETS;

            descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
        }

        Entry entry;
        entry.alignedSize = alignBlockSize(paramSet.uniformBlockSize);
        const uint32_t regionCount = imageCount > 0 ? imageCount : 3;

        core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent;
        bufferRequest.size = static_cast<vk::DeviceSize>(entry.alignedSize) * regionCount;
        core::BufferUtilities::createBuffer(bufferRequest, entry.buffer, entry.allocation,
                                            device.getMemoryManager());

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        entry.descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = entry.buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = paramSet.uniformBlockSize;

        vk::WriteDescriptorSet write{};
        write.dstSet = entry.descriptorSet;
        write.dstBinding = material::PARAMETER_DESCRIPTOR_BINDING;
        write.descriptorType = vk::DescriptorType::eUniformBufferDynamic;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;
        device.getLogicalDevice().updateDescriptorSets(write, nullptr);

        entry.paramSet = std::move(paramSet);
        auto [insertedIt, inserted] = entries.emplace(materialPath, std::move(entry));
        return &insertedIt->second;
    }

    bool MaterialParameterBufferCache::updateAndBind(
        const vk::CommandBuffer& commandBuffer,
        vk::PipelineLayout pipelineLayout,
        uint32_t imageIndex,
        const std::string& materialPath,
        const material::MaterialData& materialData,
        const std::map<std::string, material::ParameterValue>* overrides)
    {
        if (!descriptorSetLayout || materialPath.empty()) return false;

        Entry* entry = getOrCreateEntry(materialPath, materialData);
        if (!entry || !entry->allocation.mappedPtr) return false;

        const uint32_t regionCount = imageCount > 0 ? imageCount : 3;
        const uint32_t region = imageIndex % regionCount;
        const uint32_t dynamicOffset = region * entry->alignedSize;

        std::byte* dst = static_cast<std::byte*>(entry->allocation.mappedPtr) + dynamicOffset;
        static const std::map<std::string, material::ParameterValue> noOverrides;
        material::writeStd140(entry->paramSet, overrides ? *overrides : noOverrides,
                              std::span<std::byte>(dst, entry->paramSet.uniformBlockSize));

        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                         material::PARAMETER_DESCRIPTOR_SET,
                                         1, &entry->descriptorSet, 1, &dynamicOffset);
        return true;
    }

    void MaterialParameterBufferCache::invalidate(const std::string& materialPath)
    {
        auto it = entries.find(materialPath);
        if (it == entries.end()) return;

        device.getLogicalDevice().waitIdle();
        destroyEntry(it->second);
        entries.erase(it);
    }

    void MaterialParameterBufferCache::invalidateAll()
    {
        if (entries.empty()) return;

        device.getLogicalDevice().waitIdle();
        for (auto& [path, entry] : entries)
        {
            destroyEntry(entry);
        }
        entries.clear();
    }

    void MaterialParameterBufferCache::destroyEntry(Entry& entry)
    {
        if (entry.descriptorSet && descriptorPool)
        {
            device.getLogicalDevice().freeDescriptorSets(descriptorPool, entry.descriptorSet);
            entry.descriptorSet = nullptr;
        }
        if (entry.buffer)
        {
            device.getLogicalDevice().destroyBuffer(entry.buffer);
            entry.buffer = nullptr;
            device.getMemoryManager().free(entry.allocation);
            entry.allocation = {};
        }
    }

    void MaterialParameterBufferCache::cleanUp()
    {
        invalidateAll();

        if (descriptorPool)
        {
            device.getLogicalDevice().destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (descriptorSetLayout)
        {
            device.getLogicalDevice().destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }
    }
}
