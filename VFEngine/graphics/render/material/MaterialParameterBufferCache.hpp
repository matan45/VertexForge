#pragma once

#include <vulkan/vulkan.hpp>
#include "../../core/VulkanMemoryManager.hpp"
#include "material/MaterialParameterSet.hpp"
#include <map>
#include <string>
#include <unordered_map>

namespace core
{
    class Device;
}

namespace material
{
    struct MaterialData;
}

namespace render::mesh
{
    // Backs the generated set-2 std140 uniform block (see material::emitGlslUniformBlock)
    // with one host-visible buffer per material. The buffer holds one aligned region per
    // swapchain image and is bound as a dynamic uniform buffer, so values written at record
    // time never race frames still in flight. Values are rewritten on every bind — they come
    // straight from the cached MaterialData, which makes live editor tweaks show up without
    // a shader recompile.
    class MaterialParameterBufferCache
    {
    private:
        struct Entry
        {
            vk::Buffer buffer;
            core::VulkanAllocation allocation;
            vk::DescriptorSet descriptorSet;
            material::MaterialParameterSet paramSet;
            uint32_t alignedSize = 0;
        };

        core::Device& device;
        uint32_t imageCount = 0;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        std::unordered_map<std::string, Entry> entries;
        static constexpr int MAX_PARAMETER_DESCRIPTOR_SETS = 256;

    public:
        explicit MaterialParameterBufferCache(core::Device& device);
        ~MaterialParameterBufferCache();

        MaterialParameterBufferCache(const MaterialParameterBufferCache&) = delete;
        MaterialParameterBufferCache& operator=(const MaterialParameterBufferCache&) = delete;

        // Creates the set-2 layout; call before the pipeline layout is built.
        void initLayout();
        void setImageCount(uint32_t count) { imageCount = count; }

        vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

        // Writes current parameter values (graph defaults merged with optional overrides)
        // into the region for imageIndex and binds set 2 with the matching dynamic offset.
        // Returns false when the material exposes no value parameters (nothing bound).
        bool updateAndBind(const vk::CommandBuffer& commandBuffer,
                           vk::PipelineLayout pipelineLayout,
                           uint32_t imageIndex,
                           const std::string& materialPath,
                           const material::MaterialData& materialData,
                           const std::map<std::string, material::ParameterValue>* overrides = nullptr);

        void invalidate(const std::string& materialPath);
        void invalidateAll();
        void cleanUp();

    private:
        Entry* getOrCreateEntry(const std::string& materialPath, const material::MaterialData& materialData);
        void destroyEntry(Entry& entry);
        uint32_t alignBlockSize(uint32_t blockSize) const;
    };
}
