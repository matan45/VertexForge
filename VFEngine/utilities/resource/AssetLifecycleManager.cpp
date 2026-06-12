#include "AssetLifecycleManager.hpp"
#include <algorithm>

namespace resource {

	AssetLifecycleManager& AssetLifecycleManager::instance()
	{
		static AssetLifecycleManager inst;
		return inst;
	}

	void AssetLifecycleManager::acquire(const asset::AssetGUID& guid, AssetType type, size_t estimatedMemoryBytes)
	{
		std::scoped_lock lock(registryMutex);

		auto it = registry.find(guid);
		if (it != registry.end()) {
			auto& entry = it->second;
			entry.refCount++;

			if (entry.state == AssetState::PendingRelease) {
				entry.state = AssetState::Active;
				entry.graceTimeRemaining = 0.0f;

				std::erase(pendingReleaseQueue, guid);
			}

			if (estimatedMemoryBytes > 0) {
				entry.estimatedMemoryBytes = estimatedMemoryBytes;
			}
		}
		else {
			AssetEntry entry;
			entry.guid = guid;
			entry.type = type;
			entry.state = AssetState::Active;
			entry.refCount = 1;
			entry.estimatedMemoryBytes = estimatedMemoryBytes;
			entry.graceTimeRemaining = 0.0f;
			registry[guid] = std::move(entry);
		}
	}

	void AssetLifecycleManager::release(const asset::AssetGUID& guid)
	{
		std::scoped_lock lock(registryMutex);

		auto it = registry.find(guid);
		if (it == registry.end()) {
			return;
		}

		auto& entry = it->second;
		if (entry.refCount == 0) {
			return;
		}

		entry.refCount--;

		if (entry.refCount == 0) {
			entry.state = AssetState::PendingRelease;
			entry.graceTimeRemaining = gracePeriodSeconds;
			pendingReleaseQueue.push_back(guid);
		}
	}

	void AssetLifecycleManager::tick(float deltaTime)
	{
		std::vector<std::pair<asset::AssetGUID, AssetType>> toNotify;
		std::vector<std::vector<asset::AssetGUID>> dependencyChildren;

		{
			std::scoped_lock lock(registryMutex);

			// Budget pressure: while over budget, pending releases skip the
			// remaining grace (queue order = oldest unreferenced first) until
			// the projected total is back under
			size_t totalBytes = 0;
			bool budgetEnabled = budgetConfig.totalBudgetBytes > 0;
			if (budgetEnabled) {
				for (const auto& [g, e] : registry) {
					totalBytes += e.estimatedMemoryBytes;
				}
			}
			bool pressure = budgetEnabled && totalBytes > budgetConfig.totalBudgetBytes;
			uint32_t releaseCap = pressure ? budgetConfig.pressureMaxReleasesPerFrame : maxReleasesPerFrame;

			uint32_t releasedThisFrame = 0;
			std::vector<asset::AssetGUID> toRelease;

			for (auto& guid : pendingReleaseQueue) {
				auto it = registry.find(guid);
				if (it == registry.end() || it->second.state != AssetState::PendingRelease) {
					continue;
				}

				it->second.graceTimeRemaining -= deltaTime;

				bool forced = pressure && totalBytes > budgetConfig.totalBudgetBytes;
				if (it->second.graceTimeRemaining <= 0.0f || forced) {
					toRelease.push_back(guid);
					size_t bytes = it->second.estimatedMemoryBytes;
					totalBytes -= (bytes < totalBytes) ? bytes : totalBytes;
				}
			}

			for (const auto& guid : toRelease) {
				if (releasedThisFrame >= releaseCap) {
					break;
				}

				auto it = registry.find(guid);
				if (it != registry.end()) {
					toNotify.emplace_back(guid, it->second.type);
					registry.erase(it);
					releasedThisFrame++;

					std::vector<asset::AssetGUID> children;
					auto depIt = dependencies.find(guid);
					if (depIt != dependencies.end()) {
						for (const auto& dep : depIt->second) {
							children.push_back(dep.child);
						}
						dependencies.erase(depIt);
					}
					dependencyChildren.push_back(std::move(children));
				}
			}

			std::erase_if(pendingReleaseQueue, [this](const asset::AssetGUID& guid) {
				return !registry.contains(guid) || registry[guid].state != AssetState::PendingRelease;
			});
		}

		for (size_t i = 0; i < toNotify.size(); ++i) {
			const auto& [guid, type] = toNotify[i];

			if (releaseCallback) {
				releaseCallback(guid, type);
			}

			if (i < dependencyChildren.size()) {
				for (const auto& child : dependencyChildren[i]) {
					release(child);
				}
			}
		}
	}

	void AssetLifecycleManager::forceRelease(const asset::AssetGUID& guid)
	{
		asset::AssetGUID releaseGuid;
		AssetType releaseType{};

		{
			std::scoped_lock lock(registryMutex);

			auto it = registry.find(guid);
			if (it == registry.end()) {
				return;
			}

			releaseGuid = guid;
			releaseType = it->second.type;
			registry.erase(it);
			std::erase(pendingReleaseQueue, guid);
		}

		if (releaseCallback) {
			releaseCallback(releaseGuid, releaseType);
		}
		removeDependencies(releaseGuid);
	}

	void AssetLifecycleManager::addDependency(const asset::AssetGUID& parent, const asset::AssetGUID& child, AssetType childType)
	{
		std::scoped_lock lock(registryMutex);

		auto& deps = dependencies[parent];
		for (const auto& dep : deps) {
			if (dep.child == child) return;
		}
		deps.push_back({child, childType});

		auto it = registry.find(child);
		if (it != registry.end()) {
			it->second.refCount++;
			if (it->second.state == AssetState::PendingRelease) {
				it->second.state = AssetState::Active;
				it->second.graceTimeRemaining = 0.0f;
				std::erase(pendingReleaseQueue, child);
			}
		}
		else {
			AssetEntry entry;
			entry.guid = child;
			entry.type = childType;
			entry.state = AssetState::Active;
			entry.refCount = 1;
			registry[child] = std::move(entry);
		}
	}

	void AssetLifecycleManager::removeDependencies(const asset::AssetGUID& parent)
	{
		std::vector<asset::AssetGUID> childGuids;
		{
			std::scoped_lock lock(registryMutex);
			auto it = dependencies.find(parent);
			if (it == dependencies.end()) return;

			for (const auto& dep : it->second) {
				childGuids.push_back(dep.child);
			}
			dependencies.erase(it);
		}

		for (const auto& child : childGuids) {
			release(child);
		}
	}

	void AssetLifecycleManager::setReleaseCallback(ReleaseCallback callback)
	{
		std::scoped_lock lock(registryMutex);
		releaseCallback = std::move(callback);
	}

	void AssetLifecycleManager::setMemoryBudget(const MemoryBudgetConfig& config)
	{
		std::scoped_lock lock(registryMutex);
		budgetConfig = config;
	}

	MemoryBudgetConfig AssetLifecycleManager::getMemoryBudget() const
	{
		std::scoped_lock lock(registryMutex);
		return budgetConfig;
	}

	size_t AssetLifecycleManager::getTotalTrackedBytes() const
	{
		std::scoped_lock lock(registryMutex);
		size_t total = 0;
		for (const auto& [guid, entry] : registry) {
			total += entry.estimatedMemoryBytes;
		}
		return total;
	}

	bool AssetLifecycleManager::isOverBudget() const
	{
		std::scoped_lock lock(registryMutex);
		if (budgetConfig.totalBudgetBytes == 0) return false;

		size_t total = 0;
		for (const auto& [guid, entry] : registry) {
			total += entry.estimatedMemoryBytes;
		}
		return total > budgetConfig.totalBudgetBytes;
	}

	AssetEntry AssetLifecycleManager::getAssetEntry(const asset::AssetGUID& guid) const
	{
		std::scoped_lock lock(registryMutex);

		auto it = registry.find(guid);
		if (it != registry.end()) {
			return it->second;
		}
		return {};
	}

	std::vector<AssetEntry> AssetLifecycleManager::getAllAssets() const
	{
		std::scoped_lock lock(registryMutex);

		std::vector<AssetEntry> result;
		result.reserve(registry.size());
		for (const auto& [guid, entry] : registry) {
			result.push_back(entry);
		}
		return result;
	}

	std::vector<AssetEntry> AssetLifecycleManager::getPendingReleases() const
	{
		std::scoped_lock lock(registryMutex);

		std::vector<AssetEntry> result;
		for (const auto& guid : pendingReleaseQueue) {
			auto it = registry.find(guid);
			if (it != registry.end() && it->second.state == AssetState::PendingRelease) {
				result.push_back(it->second);
			}
		}
		return result;
	}

	bool AssetLifecycleManager::isTracked(const asset::AssetGUID& guid) const
	{
		std::scoped_lock lock(registryMutex);
		return registry.contains(guid);
	}

	void AssetLifecycleManager::clear()
	{
		std::scoped_lock lock(registryMutex);
		registry.clear();
		pendingReleaseQueue.clear();
		dependencies.clear();
	}

}
