#include "RenderGraph.hpp"
#include "ResourceTracker.hpp"
#include "BarrierBatcher.hpp"
#include "RenderGraphProfiler.hpp"
#include "../../core/Device.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <queue>
#include <cassert>

namespace render::graph
{
    // --- PassBuilder ---

    PassBuilder::PassBuilder(RenderGraph& graph, PassNode& node)
        : graph(graph), node(node)
    {
    }

    void PassBuilder::read(ResourceHandle handle, ResourceUsage usage)
    {
        ResourceAccessInfo access{};
        access.handle = handle;
        access.usage = usage;
        access.isWrite = false;
        node.resourceAccesses.push_back(access);
    }

    ResourceHandle PassBuilder::write(ResourceHandle handle, ResourceUsage usage)
    {
        auto& resource = graph.getResourceNode(handle.index);
        resource.currentVersion++;
        resource.writerPass = node.index;

        ResourceHandle newHandle{};
        newHandle.index = handle.index;
        newHandle.version = resource.currentVersion;

        ResourceAccessInfo access{};
        access.handle = newHandle;
        access.usage = usage;
        access.isWrite = true;
        node.resourceAccesses.push_back(access);

        return newHandle;
    }

    ResourceHandle PassBuilder::create(const ImageResourceDesc& desc, ResourceUsage usage)
    {
        ResourceHandle handle = graph.createResource(desc);

        auto& resource = graph.getResourceNode(handle.index);
        resource.writerPass = node.index;

        ResourceAccessInfo access{};
        access.handle = handle;
        access.usage = usage;
        access.isWrite = true;
        node.resourceAccesses.push_back(access);

        return handle;
    }

    void PassBuilder::setType(PassType type) { node.type = type; }
    void PassBuilder::setQueue(QueueType queue) { node.queue = queue; }
    void PassBuilder::setSegment(HookSegment segment) { node.segment = segment; }
    void PassBuilder::setSideEffect() { node.hasSideEffect = true; }

    // --- RenderGraph ---

    RenderGraph::RenderGraph(core::Device& device)
        : device(device),
          tracker(std::make_unique<ResourceTracker>()),
          barrierBatcher(std::make_unique<BarrierBatcher>())
    {
    }

    RenderGraph::~RenderGraph() = default;

    PassBuilder RenderGraph::addPass(const std::string& name, PassExecuteCallback callback)
    {
        PassNode node{};
        node.index = static_cast<uint32_t>(passes.size());
        node.name = name;
        node.execute = std::move(callback);
        passes.push_back(std::move(node));
        compiled = false;
        return PassBuilder(*this, passes.back());
    }

    ResourceHandle RenderGraph::importImage(vk::Image image, vk::ImageView view,
                                            vk::ImageLayout currentLayout,
                                            const ImageResourceDesc& desc)
    {
        ResourceNode node{};
        node.desc = desc;
        node.imported = true;
        node.importedImage = image;
        node.importedView = view;
        node.importedInitialLayout = currentLayout;
        node.physicalImage = image;
        node.physicalView = view;

        ResourceHandle handle{};
        handle.index = static_cast<uint32_t>(resources.size());
        handle.version = 0;
        resources.push_back(std::move(node));
        return handle;
    }

    ResourceHandle RenderGraph::createResource(const ImageResourceDesc& desc)
    {
        ResourceNode node{};
        node.desc = desc;

        ResourceHandle handle{};
        handle.index = static_cast<uint32_t>(resources.size());
        handle.version = 0;
        resources.push_back(std::move(node));
        return handle;
    }

    void RenderGraph::compile()
    {
        if (passes.empty())
        {
            compiled = true;
            return;
        }

        buildDependencyDAG();
        topologicalSort();
        computeResourceLifetimes();
        planBarriers();

        compiled = true;
    }

    void RenderGraph::execute(vk::CommandBuffer cmd, uint32_t imageIndex)
    {
        if (!compiled)
        {
            vfLogError("RenderGraph::execute() called before compile()");
            return;
        }

        if (profiler && profiler->isEnabled())
        {
            profiler->beginFrame(cmd, imageIndex);
        }

        for (uint32_t i = 0; i < sortedOrder.size(); ++i)
        {
            uint32_t passIdx = sortedOrder[i];
            auto& pass = passes[passIdx];

            if (pass.culled)
                continue;

            // Flush barriers for this pass
            barrierBatcher->flush(cmd, passIdx);

            // Profiling
            if (profiler && profiler->isEnabled())
            {
                profiler->beginPass(cmd, imageIndex, i, pass.name);
            }

            // Execute the pass callback
            pass.execute(cmd, imageIndex);

            if (profiler && profiler->isEnabled())
            {
                profiler->endPass(cmd, imageIndex, i);
            }
        }

        if (profiler && profiler->isEnabled())
        {
            profiler->endFrame(cmd, imageIndex);
        }
    }

    void RenderGraph::reset()
    {
        passes.clear();
        resources.clear();
        sortedOrder.clear();
        dependencies.clear();
        tracker->reset();
        barrierBatcher->clear();
        compiled = false;
    }

    void RenderGraph::setProfiler(RenderGraphProfiler* p) { profiler = p; }
    void RenderGraph::setTransientPool(TransientResourcePool* pool) { transientPool = pool; }

    ResourceNode& RenderGraph::getResourceNode(uint32_t index)
    {
        assert(index < resources.size());
        return resources[index];
    }

    uint32_t RenderGraph::getResourceCount() const
    {
        return static_cast<uint32_t>(resources.size());
    }

    void RenderGraph::buildDependencyDAG()
    {
        dependencies.resize(passes.size());
        for (auto& deps : dependencies)
            deps.clear();

        for (auto& pass : passes)
        {
            for (const auto& access : pass.resourceAccesses)
            {
                if (!access.isWrite && access.handle.isValid())
                {
                    // This pass reads a resource. Find the writer pass.
                    auto& resource = resources[access.handle.index];
                    if (resource.writerPass != UINT32_MAX && resource.writerPass != pass.index)
                    {
                        dependencies[pass.index].push_back(resource.writerPass);
                    }
                }
            }
        }
    }

    void RenderGraph::topologicalSort()
    {
        uint32_t numPasses = static_cast<uint32_t>(passes.size());
        std::vector<uint32_t> inDegree(numPasses, 0);

        // Build reverse adjacency (who depends on whom) and count in-degrees
        std::vector<std::vector<uint32_t>> dependents(numPasses);
        for (uint32_t i = 0; i < numPasses; ++i)
        {
            for (uint32_t dep : dependencies[i])
            {
                dependents[dep].push_back(i);
                inDegree[i]++;
            }
        }

        // Compute layout affinity score: how many resources would avoid layout transitions
        // if this pass runs immediately after the last scheduled pass
        auto computeAffinityScore = [this](uint32_t passIdx,
                                            const std::unordered_map<uint32_t, vk::ImageLayout>& currentLayouts) -> int
        {
            int score = 0;
            for (const auto& access : passes[passIdx].resourceAccesses)
            {
                if (!access.handle.isValid()) continue;
                auto mapping = getUsageMapping(access.usage);
                auto it = currentLayouts.find(access.handle.index);
                if (it != currentLayouts.end() && it->second == mapping.layout)
                    score++; // No layout transition needed
            }
            return score;
        };

        // Track resource layouts as we schedule passes (for affinity scoring)
        std::unordered_map<uint32_t, vk::ImageLayout> currentLayouts;
        PassType lastPassType = PassType::Graphics;

        // Custom greedy selection from ready set with heuristics
        std::vector<uint32_t> readySet;
        for (uint32_t i = 0; i < numPasses; ++i)
        {
            if (inDegree[i] == 0)
                readySet.push_back(i);
        }

        sortedOrder.clear();
        sortedOrder.reserve(numPasses);

        while (!readySet.empty())
        {
            // Score each ready candidate and pick the best
            uint32_t bestIdx = 0;
            int bestScore = std::numeric_limits<int>::min();

            for (uint32_t ri = 0; ri < readySet.size(); ++ri)
            {
                uint32_t candidate = readySet[ri];
                int score = 0;

                // 1. Segment constraint (highest priority): lower segment first
                // Each segment unit difference is worth 10000 points
                auto seg = static_cast<int>(passes[candidate].segment);
                score -= seg * 10000;

                // 2. Layout affinity: prefer passes that avoid transitions
                score += computeAffinityScore(candidate, currentLayouts) * 100;

                // 3. Queue grouping: prefer same pass type as previous
                if (passes[candidate].type == lastPassType)
                    score += 50;

                // 4. Declaration order tiebreaker (preserves hand-tuned order)
                score -= static_cast<int>(candidate);

                if (score > bestScore)
                {
                    bestScore = score;
                    bestIdx = ri;
                }
            }

            uint32_t current = readySet[bestIdx];
            readySet.erase(readySet.begin() + bestIdx);

            passes[current].sortedIndex = static_cast<uint32_t>(sortedOrder.size());
            sortedOrder.push_back(current);

            // Update layout tracking
            for (const auto& access : passes[current].resourceAccesses)
            {
                if (!access.handle.isValid()) continue;
                auto mapping = getUsageMapping(access.usage);
                currentLayouts[access.handle.index] = mapping.layout;
            }
            lastPassType = passes[current].type;

            // Release dependents
            for (uint32_t dep : dependents[current])
            {
                inDegree[dep]--;
                if (inDegree[dep] == 0)
                    readySet.push_back(dep);
            }
        }

        if (sortedOrder.size() != numPasses)
        {
            vfLogError("RenderGraph: Cycle detected in pass dependencies! {} of {} passes sorted.",
                      sortedOrder.size(), numPasses);
        }
    }

    void RenderGraph::computeResourceLifetimes()
    {
        for (auto& resource : resources)
        {
            resource.firstUsePass = UINT32_MAX;
            resource.lastUsePass = 0;
        }

        for (uint32_t sortIdx = 0; sortIdx < sortedOrder.size(); ++sortIdx)
        {
            uint32_t passIdx = sortedOrder[sortIdx];
            auto& pass = passes[passIdx];

            for (const auto& access : pass.resourceAccesses)
            {
                if (!access.handle.isValid()) continue;
                auto& resource = resources[access.handle.index];
                resource.firstUsePass = std::min(resource.firstUsePass, sortIdx);
                resource.lastUsePass = std::max(resource.lastUsePass, sortIdx);
            }
        }
    }

    void RenderGraph::planBarriers()
    {
        barrierBatcher->clear();

        // Initialize tracker state for imported resources
        for (uint32_t i = 0; i < resources.size(); ++i)
        {
            auto& resource = resources[i];
            if (resource.imported)
            {
                tracker->initResource(i, resource.importedInitialLayout);
            }
            else
            {
                tracker->initResource(i, vk::ImageLayout::eUndefined);
            }
        }

        // Walk sorted passes and plan barriers
        for (uint32_t sortIdx = 0; sortIdx < sortedOrder.size(); ++sortIdx)
        {
            uint32_t passIdx = sortedOrder[sortIdx];
            auto& pass = passes[passIdx];

            if (pass.culled) continue;

            for (const auto& access : pass.resourceAccesses)
            {
                if (!access.handle.isValid()) continue;

                auto& resource = resources[access.handle.index];
                auto mapping = getUsageMapping(access.usage);

                auto barrier = tracker->transition(
                    access.handle.index,
                    mapping.stage,
                    mapping.access,
                    mapping.layout,
                    access.isWrite,
                    resource.physicalImage,
                    resource.desc.aspectMask);

                if (barrier.has_value())
                {
                    barrierBatcher->add(passIdx, barrier.value());
                }
            }
        }
    }
}
