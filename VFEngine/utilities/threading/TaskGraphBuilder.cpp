#include "../print/Log.hpp"
#include "TaskGraphBuilder.hpp"
#include "TaskGraphImpl.hpp"

#include <algorithm>
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

		// Identify root tasks (in-degree 0). NOTE: topologicalSort() consumed (zeroed) the
		// inDegree vector via Kahn's algorithm, so recompute a fresh count from the edges.
		{
			std::vector<uint32_t> rootInDegree(nodeCount, 0);
			for (auto& e : impl.edges) {
				rootInDegree[e.to]++;
			}
			for (uint32_t i = 0; i < nodeCount; ++i) {
				if (rootInDegree[i] == 0) {
					impl.rootIndices.push_back(i);
				}
			}
		}

		// Identify leaf tasks (out-degree 0)
		for (uint32_t i = 0; i < nodeCount; ++i) {
			if (adjacency[i].empty()) {
				impl.leafIndices.push_back(i);
			}
		}

		// Build one persistent enkiTS TaskSet per non-pinned node. Each captures stable
		// pointers into impl (nodes/profileData never move after build, and impl itself is
		// owned by a stable unique_ptr), so re-adding the same object every frame needs no
		// allocation. Pinned nodes keep a null slot here (built below as pinned tasks).
		impl.cachedTaskSets.resize(nodeCount);
		for (uint32_t i = 0; i < nodeCount; ++i) {
			if (impl.nodes[i].pinned) {
				continue;
			}
			const TaskNodeInfo* node = &impl.nodes[i];
			TaskProfileEntry* entry = &impl.profileData[i];
			const auto* baseTimePtr = &impl.baseTime;
			const bool* profilingFlag = &impl.profilingEnabled;

			auto taskSet = std::make_unique<enki::TaskSet>(1u,
				[node, entry, baseTimePtr, profilingFlag](enki::TaskSetPartition, uint32_t threadNum) {
					if (*profilingFlag) {
						auto start = std::chrono::high_resolution_clock::now();
						node->fn();
						auto end = std::chrono::high_resolution_clock::now();
						entry->startTimeNs = static_cast<uint64_t>(
							std::chrono::duration_cast<std::chrono::nanoseconds>(start - *baseTimePtr).count());
						entry->endTimeNs = static_cast<uint64_t>(
							std::chrono::duration_cast<std::chrono::nanoseconds>(end - *baseTimePtr).count());
						entry->threadId = threadNum;
					}
					else {
						node->fn();
					}
				});
			taskSet->m_Priority = static_cast<enki::TaskPriority>(
				static_cast<uint32_t>(impl.nodes[i].priority));
			impl.cachedTaskSets[i] = std::move(taskSet);
		}

		// Build one persistent pinned task (thread 0) per pinned node, mirroring the
		// non-pinned timing capture but always recording threadId 0 (the main thread runs
		// pinned tasks while it waits on the terminal in execute()).
		impl.cachedPinnedTasks.resize(nodeCount);
		for (uint32_t i = 0; i < nodeCount; ++i) {
			if (!impl.nodes[i].pinned) {
				continue;
			}
			const TaskNodeInfo* node = &impl.nodes[i];
			TaskProfileEntry* entry = &impl.profileData[i];
			const auto* baseTimePtr = &impl.baseTime;
			const bool* profilingFlag = &impl.profilingEnabled;

			auto pinned = std::make_unique<enki::LambdaPinnedTask>(0u,
				[node, entry, baseTimePtr, profilingFlag]() {
					if (*profilingFlag) {
						auto start = std::chrono::high_resolution_clock::now();
						node->fn();
						auto end = std::chrono::high_resolution_clock::now();
						entry->startTimeNs = static_cast<uint64_t>(
							std::chrono::duration_cast<std::chrono::nanoseconds>(start - *baseTimePtr).count());
						entry->endTimeNs = static_cast<uint64_t>(
							std::chrono::duration_cast<std::chrono::nanoseconds>(end - *baseTimePtr).count());
						entry->threadId = 0;
					}
					else {
						node->fn();
					}
				});
			pinned->m_Priority = static_cast<enki::TaskPriority>(
				static_cast<uint32_t>(impl.nodes[i].priority));
			impl.cachedPinnedTasks[i] = std::move(pinned);
		}

		// Resolve a node index to its persistent completable (whichever kind it is).
		auto completableOf = [&impl](uint32_t idx) -> enki::ICompletable* {
			if (impl.nodes[idx].pinned) {
				return impl.cachedPinnedTasks[idx].get();
			}
			return impl.cachedTaskSets[idx].get();
		};

		// Wire native enkiTS dependencies once: each node depends on its predecessors, so the
		// scheduler auto-launches a node the moment all its predecessors complete (no manual
		// per-layer barrier, full cross-layer overlap). Completion counts auto-reset, so the
		// graph is reused every frame by re-adding only the roots in execute().
		std::vector<std::vector<uint32_t>> predecessors(nodeCount);
		for (auto& e : impl.edges) {
			predecessors[e.to].push_back(e.from);
		}

		impl.nodeDeps.resize(nodeCount); // outer sized ONCE - inner buffers must stay address-stable
		for (uint32_t i = 0; i < nodeCount; ++i) {
			auto& preds = predecessors[i];
			if (preds.empty()) {
				continue;
			}
			impl.nodeDeps[i].resize(preds.size());
			for (size_t k = 0; k < preds.size(); ++k) {
				completableOf(i)->SetDependency(impl.nodeDeps[i][k], completableOf(preds[k]));
			}
		}

		// Terminal sentinel depends on every leaf (out-degree 0, including isolated nodes),
		// giving execute() a single wait target that completes only when the whole graph does.
		impl.terminalDeps.resize(impl.leafIndices.size());
		for (size_t k = 0; k < impl.leafIndices.size(); ++k) {
			impl.terminal.SetDependency(impl.terminalDeps[k], completableOf(impl.leafIndices[k]));
		}

		return graph;
	}

}
