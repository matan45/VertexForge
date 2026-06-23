#include "CpuMemoryServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/memory/CpuMemoryEvents.hpp"
#include "../../events/editor/EditorSettingsEvents.hpp"
#include "cpumem/CpuMemoryManager.hpp"

namespace services {

	void CpuMemoryServiceImpl::registerEventHandlers()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		dispatcher.registerCommandHandler<events::memory::SetCpuMemoryBudgetCommand>(
			[](const events::memory::SetCpuMemoryBudgetCommand& cmd) -> bool
			{
				::memory::CpuMemoryManager::instance().setBudget(cmd.budgetBytes);

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
