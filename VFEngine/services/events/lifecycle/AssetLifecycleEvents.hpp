#pragma once
#include "../EventTypes.hpp"
#include "resource/AssetTypes.hpp"
#include <string>
#include <vector>

namespace events::lifecycle {

	// ============================================================
	// NOTIFICATIONS
	// ============================================================

	struct AssetReleaseReadyNotification : INotification {
		std::string path;
		resource::AssetType type{};

		std::string_view getName() const override { return "AssetReleaseReady"; }
	};

	struct AssetAcquiredNotification : INotification {
		std::string path;
		resource::AssetType type{};
		uint32_t newRefCount = 0;

		std::string_view getName() const override { return "AssetAcquired"; }
	};

	struct AssetReleasedNotification : INotification {
		std::string path;
		resource::AssetType type{};
		uint32_t newRefCount = 0;

		std::string_view getName() const override { return "AssetReleased"; }
	};

	// ============================================================
	// COMMANDS
	// ============================================================

	struct ForceReleaseAssetCommand : ::events::ICommand<void> {
		std::string path;

		std::string_view getName() const override { return "ForceReleaseAsset"; }
	};

	struct SetMemoryBudgetCommand : ::events::ICommand<void> {
		size_t totalBudgetBytes = 0; // 0 disables the budget

		std::string_view getName() const override { return "SetMemoryBudget"; }
	};

	// ============================================================
	// QUERIES
	// ============================================================

	struct QueryAssetStatsQuery : ::events::IQuery<std::vector<resource::AssetEntry>> {
		std::string_view getName() const override { return "QueryAssetStats"; }
	};

	struct QueryPendingReleasesQuery : ::events::IQuery<std::vector<resource::AssetEntry>> {
		std::string_view getName() const override { return "QueryPendingReleases"; }
	};

	struct QueryAssetEntryQuery : ::events::IQuery<resource::AssetEntry> {
		std::string path;

		std::string_view getName() const override { return "QueryAssetEntry"; }
	};

	struct MemoryBudgetStatus {
		size_t totalBudgetBytes = 0;
		size_t trackedBytes = 0;
		bool overBudget = false;
	};

	struct QueryMemoryBudgetQuery : ::events::IQuery<MemoryBudgetStatus> {
		std::string_view getName() const override { return "QueryMemoryBudget"; }
	};

}
