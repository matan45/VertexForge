#include "../print/Log.hpp"
#include "TaskGraphBuilder.hpp"
#include "TaskGraphImpl.hpp"

#include <queue>
#include <unordered_set>

namespace threading {

	struct TaskNodeDef {
		std::string name;
		std::function<void()> fn;
		JobPriority priority = JobPriority::NORMAL;
		bool pinned = false;
	};

	struct EdgeDef {
		uint32_t from; // dependency (must complete first)
		uint32_t to;   // dependent (runs after 'from')
	};

	struct TaskGraphBuilder::BuilderImpl {
		std::vector<TaskNodeDef> nodes;
		std::vector<EdgeDef> edges;
	};

	TaskGraphBuilder::TaskGraphBuilder() : pImpl(std::make_unique<BuilderImpl>()) {}
	TaskGraphBuilder::~TaskGraphBuilder() = default;

	TaskHandle TaskGraphBuilder::task(std::string_view name, std::function<void()> fn,
		JobPriority priority)
	{
		TaskHandle handle = static_cast<TaskHandle>(pImpl->nodes.size());
		pImpl->nodes.push_back({ std::string(name), std::move(fn), priority, false });
		return handle;
	}

	TaskHandle TaskGraphBuilder::pinnedTask(std::string_view name, std::function<void()> fn,
		JobPriority priority)
	{
		TaskHandle handle = static_cast<TaskHandle>(pImpl->nodes.size());
		pImpl->nodes.push_back({ std::string(name), std::move(fn), priority, true });
		return handle;
	}

	TaskGraphBuilder& TaskGraphBuilder::depends(TaskHandle dependent, TaskHandle dependency)
	{
		uint32_t count = static_cast<uint32_t>(pImpl->nodes.size());
		if (dependent >= count || dependency >= count) {
			vfLogError("[TaskGraphBuilder] Invalid task handle in depends({}, {})", dependent, dependency);
			return *this;
		}
		if (dependent == dependency) {
			vfLogError("[TaskGraphBuilder] Task '{}' cannot depend on itself",
				pImpl->nodes[dependent].name);
			return *this;
		}
		pImpl->edges.push_back({ dependency, dependent });
		return *this;
	}

	// Topological sort via Kahn's algorithm. Returns empty if cycle detected.
	static std::vector<uint32_t> topologicalSort(uint32_t nodeCount,
		const std::vector<EdgeDef>& edges,
		std::vector<std::vector<uint32_t>>& adjOut,
		std::vector<uint32_t>& inDegreeOut)
	{
		adjOut.assign(nodeCount, {});
		inDegreeOut.assign(nodeCount, 0);

		for (auto& e : edges) {
			adjOut[e.from].push_back(e.to);
			inDegreeOut[e.to]++;
		}

		std::queue<uint32_t> q;
		for (uint32_t i = 0; i < nodeCount; ++i) {
			if (inDegreeOut[i] == 0) q.push(i);
		}

		std::vector<uint32_t> order;
		order.reserve(nodeCount);
		while (!q.empty()) {
			uint32_t node = q.front();
			q.pop();
			order.push_back(node);
			for (uint32_t neighbor : adjOut[node]) {
				if (--inDegreeOut[neighbor] == 0) {
					q.push(neighbor);
				}
			}
		}

		if (order.size() != nodeCount) {
			return {}; // cycle detected
		}
		return order;
	}

	std::unique_ptr<TaskGraph> TaskGraphBuilder::build()
	{
		uint32_t nodeCount = static_cast<uint32_t>(pImpl->nodes.size());
		if (nodeCount == 0) {
			vfLogWarning("[TaskGraphBuilder] Empty task graph");
			return nullptr;
		}

		// Topological sort for cycle detection
		std::vector<std::vector<uint32_t>> adjacency;
		std::vector<uint32_t> inDegree;
		auto order = topologicalSort(nodeCount, pImpl->edges, adjacency, inDegree);

		if (order.empty()) {
			vfLogError("[TaskGraphBuilder] Cycle detected in task graph - cannot build");
			return nullptr;
		}

		auto* scheduler = JobSystem::instance().getScheduler();

		// Build the TaskGraph - store graph definition for per-frame rebuilding
		auto graph = std::unique_ptr<TaskGraph>(new TaskGraph());
		auto& impl = *graph->pImpl;
		impl.scheduler = scheduler;
		impl.adjacency = adjacency;

		// Transfer node definitions
		impl.nodes.reserve(nodeCount);
		for (auto& nodeDef : pImpl->nodes) {
			impl.nodes.push_back({
				nodeDef.name,
				std::move(nodeDef.fn),
				nodeDef.priority,
				nodeDef.pinned
			});
		}

		// Transfer edge definitions
		impl.edges.reserve(pImpl->edges.size());
		for (auto& edge : pImpl->edges) {
			impl.edges.push_back({ edge.from, edge.to });
		}

		// Cache task names
		impl.taskNames.reserve(nodeCount);
		for (auto& node : impl.nodes) {
			impl.taskNames.push_back(node.name);
		}

		// Initialize profile data
		impl.profileData.resize(nodeCount);
		for (uint32_t i = 0; i < nodeCount; ++i) {
			impl.profileData[i].name = impl.nodes[i].name;
			impl.profileData[i].priority = impl.nodes[i].priority;
		}

		// Identify root tasks (in-degree 0)
		for (uint32_t i = 0; i < nodeCount; ++i) {
			if (inDegree[i] == 0) {
				impl.rootIndices.push_back(i);
			}
		}

		// Identify leaf tasks (out-degree 0)
		for (uint32_t i = 0; i < nodeCount; ++i) {
			if (adjacency[i].empty()) {
				impl.leafIndices.push_back(i);
			}
		}

		// Pre-compute topological layers (cached for every frame's execute())
		// Note: inDegree was zeroed by topologicalSort(), so recompute from edges
		{
			std::vector<uint32_t> layerInDegree(nodeCount, 0);
			for (auto& e : pImpl->edges) {
				layerInDegree[e.to]++;
			}
			std::vector<uint32_t> currentLayer;
			for (uint32_t i = 0; i < nodeCount; ++i) {
				if (layerInDegree[i] == 0) currentLayer.push_back(i);
			}

			while (!currentLayer.empty()) {
				impl.layers.push_back(currentLayer);
				std::vector<uint32_t> nextLayer;
				for (uint32_t node : currentLayer) {
					for (uint32_t dep : adjacency[node]) {
						if (--layerInDegree[dep] == 0) {
							nextLayer.push_back(dep);
						}
					}
				}
				currentLayer = std::move(nextLayer);
			}
		}

		return graph;
	}

}
