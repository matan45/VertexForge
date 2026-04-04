#pragma once

#include "RenderGraphTypes.hpp"
#include "RenderGraphPass.hpp"
#include <vector>
#include <string>
#include <memory>

namespace core
{
    class Device;
}

namespace render::graph
{
    class ResourceTracker;
    class BarrierBatcher;
    class TransientResourcePool;
    class RenderGraphProfiler;

    class RenderGraph
    {
    public:
        explicit RenderGraph(core::Device& device);
        ~RenderGraph();

        // --- Declaration phase (called each frame after reset) ---

        // Add a pass to the graph. Returns a builder for declaring resource dependencies.
        PassBuilder addPass(const std::string& name, PassExecuteCallback callback);

        // Import an external resource that lives outside the graph (scene color, depth, swapchain, etc.)
        ResourceHandle importImage(vk::Image image, vk::ImageView view,
                                   vk::ImageLayout currentLayout,
                                   const ImageResourceDesc& desc);

        // Create a virtual resource (for transient/graph-managed resources)
        ResourceHandle createResource(const ImageResourceDesc& desc);

        // --- Compilation phase ---

        // Build dependency DAG, topological sort, plan barriers, allocate transient resources
        void compile();

        // --- Execution phase ---

        // Execute all compiled passes in sorted order, inserting barriers automatically
        void execute(vk::CommandBuffer cmd, uint32_t imageIndex);

        // --- Lifecycle ---

        // Reset for a new frame. Clears all passes and virtual resources, keeps imported resources.
        void reset();

        // --- Configuration ---

        void setProfiler(RenderGraphProfiler* profiler);
        void setTransientPool(TransientResourcePool* pool);

        // --- Accessors (used by PassBuilder) ---

        ResourceNode& getResourceNode(uint32_t index);
        uint32_t getResourceCount() const;

    private:
        void buildDependencyDAG();
        void topologicalSort();
        void computeResourceLifetimes();
        void planBarriers();

        core::Device& device;

        std::vector<PassNode> passes;
        std::vector<ResourceNode> resources;
        std::vector<uint32_t> sortedOrder;

        // Adjacency list: adjacency[passIdx] = list of pass indices this pass depends on
        std::vector<std::vector<uint32_t>> dependencies;

        std::unique_ptr<ResourceTracker> tracker;
        std::unique_ptr<BarrierBatcher> barrierBatcher;
        TransientResourcePool* transientPool = nullptr;
        RenderGraphProfiler* profiler = nullptr;

        bool compiled = false;
    };
}
