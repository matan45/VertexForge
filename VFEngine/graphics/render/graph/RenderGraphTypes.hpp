#pragma once

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <string>
#include <cstdint>
#include <limits>
#include <functional>

namespace render::graph
{
    // Strongly-typed opaque handle. Index into RenderGraph::resources vector.
    // Version bumps on each write (SSA-style) so readers can track which version they depend on.
    struct ResourceHandle
    {
        uint32_t index = UINT32_MAX;
        uint32_t version = 0;

        bool isValid() const { return index != UINT32_MAX; }

        bool operator==(const ResourceHandle& other) const
        {
            return index == other.index && version == other.version;
        }

        bool operator!=(const ResourceHandle& other) const { return !(*this == other); }
    };

    // How a pass uses a resource - maps to (stage, access, layout) tuple
    enum class ResourceUsage : uint8_t
    {
        ColorAttachmentWrite,
        DepthAttachmentWrite,
        DepthAttachmentRead,
        ShaderRead,
        ShaderWrite,
        TransferSrc,
        TransferDst,
        Present,
        StorageRead,
        StorageWrite
    };

    // Maps ResourceUsage to Vulkan synchronization2 types
    struct UsageMapping
    {
        vk::PipelineStageFlags2 stage;
        vk::AccessFlags2 access;
        vk::ImageLayout layout;
    };

    inline UsageMapping getUsageMapping(ResourceUsage usage)
    {
        switch (usage)
        {
        case ResourceUsage::ColorAttachmentWrite:
            return {vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    vk::AccessFlagBits2::eColorAttachmentWrite,
                    vk::ImageLayout::eColorAttachmentOptimal};
        case ResourceUsage::DepthAttachmentWrite:
            return {vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                    vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                    vk::ImageLayout::eDepthStencilAttachmentOptimal};
        case ResourceUsage::DepthAttachmentRead:
            return {vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                    vk::AccessFlagBits2::eDepthStencilAttachmentRead,
                    vk::ImageLayout::eDepthStencilReadOnlyOptimal};
        case ResourceUsage::ShaderRead:
            return {vk::PipelineStageFlagBits2::eFragmentShader,
                    vk::AccessFlagBits2::eShaderSampledRead,
                    vk::ImageLayout::eShaderReadOnlyOptimal};
        case ResourceUsage::ShaderWrite:
            return {vk::PipelineStageFlagBits2::eFragmentShader,
                    vk::AccessFlagBits2::eShaderStorageWrite,
                    vk::ImageLayout::eGeneral};
        case ResourceUsage::TransferSrc:
            return {vk::PipelineStageFlagBits2::eCopy,
                    vk::AccessFlagBits2::eTransferRead,
                    vk::ImageLayout::eTransferSrcOptimal};
        case ResourceUsage::TransferDst:
            return {vk::PipelineStageFlagBits2::eCopy,
                    vk::AccessFlagBits2::eTransferWrite,
                    vk::ImageLayout::eTransferDstOptimal};
        case ResourceUsage::Present:
            return {vk::PipelineStageFlagBits2::eBottomOfPipe,
                    vk::AccessFlagBits2::eNone,
                    vk::ImageLayout::ePresentSrcKHR};
        case ResourceUsage::StorageRead:
            return {vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderStorageRead,
                    vk::ImageLayout::eGeneral};
        case ResourceUsage::StorageWrite:
            return {vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderStorageWrite,
                    vk::ImageLayout::eGeneral};
        }
        return {};
    }

    enum class PassType : uint8_t
    {
        Graphics,
        Compute,
        Transfer
    };

    enum class QueueType : uint8_t
    {
        Graphics,
        AsyncCompute
    };

    // Describes a virtual image resource the graph may allocate
    struct ImageResourceDesc
    {
        vk::Extent2D extent{};
        vk::Format format = vk::Format::eUndefined;
        vk::ImageUsageFlags usage{};
        vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        bool transient = false;
        std::string debugName;
    };

    // What a pass does with a specific resource
    struct ResourceAccessInfo
    {
        ResourceHandle handle;
        ResourceUsage usage;
        bool isWrite = false;
    };

    // Represents a resource in the graph (virtual or imported)
    struct ResourceNode
    {
        ImageResourceDesc desc{};

        // For imported (external) resources
        bool imported = false;
        vk::Image importedImage{};
        vk::ImageView importedView{};
        vk::ImageLayout importedInitialLayout = vk::ImageLayout::eUndefined;

        // Producer tracking: which pass+version last wrote this resource
        uint32_t writerPass = UINT32_MAX;
        uint32_t currentVersion = 0;

        // Lifetime (filled during compile)
        uint32_t firstUsePass = UINT32_MAX;
        uint32_t lastUsePass = 0;

        // Physical allocation (filled from import or TransientResourcePool)
        vk::Image physicalImage{};
        vk::ImageView physicalView{};
    };

    // Render hook segment index for ordering constraints
    enum class HookSegment : uint8_t
    {
        Scene = 0,         // Before PostScene hook
        PostScene = 1,     // Between PostScene and PrePostProcess
        PostProcess = 2,   // Between PrePostProcess and PostPostProcess
        UI = 3,            // Between PostPostProcess and Overlay
        Overlay = 4        // After Overlay hook
    };

    // Callback type for pass execution
    using PassExecuteCallback = std::function<void(vk::CommandBuffer cmd, uint32_t imageIndex)>;
}
