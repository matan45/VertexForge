#pragma once

#include "RenderGraphTypes.hpp"
#include <vector>
#include <string>

namespace render::graph
{
    class RenderGraph;

    // Describes a single render pass in the graph
    struct PassNode
    {
        uint32_t index = 0;
        std::string name;
        PassType type = PassType::Graphics;
        QueueType queue = QueueType::Graphics;
        HookSegment segment = HookSegment::Scene;

        std::vector<ResourceAccessInfo> resourceAccesses;

        PassExecuteCallback execute;

        bool hasSideEffect = false;

        // Filled during compile
        uint32_t sortedIndex = UINT32_MAX;
        bool culled = false;
    };

    // Builder pattern for declaring pass resource dependencies
    class PassBuilder
    {
    public:
        PassBuilder(RenderGraph& graph, PassNode& node);

        // Read a resource produced by a previous pass
        void read(ResourceHandle handle, ResourceUsage usage);

        // Write to a resource (bumps version on the resource)
        ResourceHandle write(ResourceHandle handle, ResourceUsage usage);

        // Create a new transient resource and write to it
        ResourceHandle create(const ImageResourceDesc& desc, ResourceUsage usage);

        // Set pass type
        void setType(PassType type);
        void setQueue(QueueType queue);
        void setSegment(HookSegment segment);

        // Mark pass as having side effects (prevents culling)
        void setSideEffect();

    private:
        RenderGraph& graph;
        PassNode& node;
    };
}
