#pragma once
#include "../EventTypes.hpp"
#include "cpumem/CpuMemorySnapshot.hpp"

namespace events::memory {

	// ============================================================
	// DTO
	// ============================================================

	struct CpuMemoryStatus
	{
		uint64_t budgetBytes = 0;        // 0 = advisory/disabled
		uint64_t trackedBytes = 0;       // CpuMemory's own categories (transient+staging)
		uint64_t reservedBytes = 0;      // outstanding pre-load reservations
		::memory::GateState gateState = ::memory::GateState::Open;
		uint32_t deferredLoadCount = 0;  // non-Critical loads currently deferred
	};

	// ============================================================
	// COMMANDS
	// ============================================================

	// Set (and persist) the CPU memory budget.  Returns true on success.
	struct SetCpuMemoryBudgetCommand : ::events::ICommand<bool>
	{
		uint64_t budgetBytes = 0; // 0 = advisory/disabled

		std::string_view getName() const override { return "SetCpuMemoryBudget"; }
	};

	// ============================================================
	// QUERIES
	// ============================================================

	struct QueryCpuMemoryStatusQuery : ::events::IQuery<CpuMemoryStatus>
	{
		std::string_view getName() const override { return "QueryCpuMemoryStatus"; }
	};

}
