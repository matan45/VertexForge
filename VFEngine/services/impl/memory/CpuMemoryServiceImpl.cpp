#include "CpuMemoryServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/memory/CpuMemoryEvents.hpp"
#include "../../events/editor/EditorSettingsEvents.hpp"
#include "../../events/lifecycle/AssetLifecycleEvents.hpp"
#include "cpumem/CpuMemoryManager.hpp"

namespace services {

	void CpuMemoryServiceImpl::registerEventHandlers()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		dispatcher.registerCommandHandler<events::memory::SetCpuMemoryBudgetCommand>(
			[](const events::memory::SetCpuMemoryBudgetCommand& cmd) -> bool
			{
				::memory::CpuMemoryManager::instance().setBudget(cmd.budgetBytes);

					// VK-1434 fix: keep the AssetLifecycleManager eviction budget in lock-step
					// with the gate budget. The gate only DEFERS new loads; eviction of
					// unreferenced (refCount==0) decoded assets is what frees the decoded total
					// and REOPENS the gate. Driving both from one value makes the gate
					// reopenable under pressure instead of stalling indefinitely.
					try
					{
						events::lifecycle::SetMemoryBudgetCommand lifeCmd;
						lifeCmd.totalBudgetBytes = static_cast<size_t>(cmd.budgetBytes);
						events::EventDispatcher::instance().execute(lifeCmd);
					}
					catch (const std::exception&)
					{
						// Lifecycle service not registered (minimal/headless config) — the gate
						// still applies; only the eviction coordination is unavailable.
					}

				// Persist via the editor settings service so the budget survives restart.
				// Read the current settings, update the memory field, then write back.
				try
				{
					auto settings = events::EventDispatcher::instance().query(
						events::editor::GetEditorSettingsQuery{});
					settings.memory.cpuMemoryBudgetBytes = cmd.budgetBytes;
					events::editor::SetEditorSettingsCommand saveCmd;
					saveCmd.settings = std::move(settings);
					events::EventDispatcher::instance().execute(saveCmd);
				}
				catch (const std::exception&)
				{
					// Settings service not yet registered (e.g. during bootstrap).
					// The budget is still applied; it will be persisted later.
				}

				return true;
			});

		dispatcher.registerQueryHandler<events::memory::QueryCpuMemoryStatusQuery>(
			[](const events::memory::QueryCpuMemoryStatusQuery&) -> events::memory::CpuMemoryStatus
			{
				auto& mgr = ::memory::CpuMemoryManager::instance();
				const auto snap = mgr.snapshot();

				events::memory::CpuMemoryStatus status;
				status.budgetBytes      = snap.budgetBytes;
				status.trackedBytes     = snap.totalTrackedBytes;
				status.reservedBytes    = snap.totalReservedBytes;
				status.gateState        = snap.gateState;
				status.deferredLoadCount = snap.deferredLoadCount;
				return status;
			});
	}

	void CpuMemoryServiceImpl::update(float /*deltaTime*/)
	{
		// No per-tick work needed for Phase 1.
	}

}
