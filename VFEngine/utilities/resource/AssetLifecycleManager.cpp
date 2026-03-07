#include "AssetLifecycleManager.hpp"
#include <algorithm>

namespace resource {

	AssetLifecycleManager& AssetLifecycleManager::instance()
	{
		static AssetLifecycleManager inst;
		return inst;
	}

	void AssetLifecycleManager::acquire(const std::string& path, AssetType type, size_t estimatedMemoryBytes)
	{
		std::scoped_lock lock(registryMutex);

		auto it = registry.find(path);
		if (it != registry.end()) {
			auto& entry = it->second;
			entry.refCount++;

			if (entry.state == AssetState::PendingRelease) {
				entry.state = AssetState::Active;
				entry.graceTimeRemaining = 0.0f;

				std::erase(pendingReleaseQueue, path);

	
			}

			if (estimatedMemoryBytes > 0) {
				entry.estimatedMemoryBytes = estimatedMemoryBytes;
			}
		}
		else {
			AssetEntry entry;
			entry.path = path;
			entry.type = type;
			entry.state = AssetState::Active;
			entry.refCount = 1;
			entry.estimatedMemoryBytes = estimatedMemoryBytes;
			entry.graceTimeRemaining = 0.0f;
			registry[path] = std::move(entry);
		}
	}

	void AssetLifecycleManager::release(const std::string& path)
	{
		std::scoped_lock lock(registryMutex);

		auto it = registry.find(path);
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
			pendingReleaseQueue.push_back(path);


		}
	}

	void AssetLifecycleManager::tick(float deltaTime)
	{
		std::vector<std::pair<std::string, AssetType>> toNotify;
		std::vector<std::vector<std::string>> dependencyChildren;

		{
			std::scoped_lock lock(registryMutex);

			uint32_t releasedThisFrame = 0;
			std::vector<std::string> toRelease;

			for (auto& path : pendingReleaseQueue) {
				auto it = registry.find(path);
				if (it == registry.end() || it->second.state != AssetState::PendingRelease) {
					continue;
				}

				it->second.graceTimeRemaining -= deltaTime;

				if (it->second.graceTimeRemaining <= 0.0f) {
					toRelease.push_back(path);
				}
			}

			for (const auto& path : toRelease) {
				if (releasedThisFrame >= maxReleasesPerFrame) {
					break;
				}

				auto it = registry.find(path);
				if (it != registry.end()) {
					toNotify.emplace_back(path, it->second.type);
					registry.erase(it);
					releasedThisFrame++;

					// Collect dependency children inside the lock to avoid TOCTOU race
					std::vector<std::string> children;
					auto depIt = dependencies.find(path);
					if (depIt != dependencies.end()) {
						for (const auto& dep : depIt->second) {
							children.push_back(dep.childPath);
						}
						dependencies.erase(depIt);
					}
					dependencyChildren.push_back(std::move(children));
				}
			}

			std::erase_if(pendingReleaseQueue, [this](const std::string& path) {
				return !registry.contains(path) || registry[path].state != AssetState::PendingRelease;
			});
		}

		// Invoke callback and cascade dependencies outside the lock
		for (size_t i = 0; i < toNotify.size(); ++i) {
			const auto& [path, type] = toNotify[i];

			if (releaseCallback) {
				releaseCallback(path, type);
			}

			// Release children collected while lock was held
			if (i < dependencyChildren.size()) {
				for (const auto& child : dependencyChildren[i]) {
					release(child);
				}
			}
		}
	}

	void AssetLifecycleManager::forceRelease(const std::string& path)
	{
		std::string releasePath;
		AssetType releaseType{};

		{
			std::scoped_lock lock(registryMutex);

			auto it = registry.find(path);
			if (it == registry.end()) {
				return;
			}

			releasePath = path;
			releaseType = it->second.type;
			registry.erase(it);
			std::erase(pendingReleaseQueue, path);
		}

		if (releaseCallback) {
			releaseCallback(releasePath, releaseType);
		}
		removeDependencies(releasePath);
	}

	void AssetLifecycleManager::addDependency(const std::string& parentPath, const std::string& childPath, AssetType childType)
	{
		std::scoped_lock lock(registryMutex);

		auto& deps = dependencies[parentPath];
		for (const auto& dep : deps) {
			if (dep.childPath == childPath) return; // Already registered
		}
		deps.push_back({childPath, childType});

		// Implicitly acquire the child
		auto it = registry.find(childPath);
		if (it != registry.end()) {
			it->second.refCount++;
			if (it->second.state == AssetState::PendingRelease) {
				it->second.state = AssetState::Active;
				it->second.graceTimeRemaining = 0.0f;
				std::erase(pendingReleaseQueue, childPath);
			}
		}
		else {
			AssetEntry entry;
			entry.path = childPath;
			entry.type = childType;
			entry.state = AssetState::Active;
			entry.refCount = 1;
			registry[childPath] = std::move(entry);
		}
	}

	void AssetLifecycleManager::removeDependencies(const std::string& parentPath)
	{
		// Collect children to release outside lock
		std::vector<std::string> childPaths;
		{
			std::scoped_lock lock(registryMutex);
			auto it = dependencies.find(parentPath);
			if (it == dependencies.end()) return;

			for (const auto& dep : it->second) {
				childPaths.push_back(dep.childPath);
			}
			dependencies.erase(it);
		}

		// Release each child (release() takes its own lock)
		for (const auto& child : childPaths) {
			release(child);
		}
	}

	void AssetLifecycleManager::processRelease(const std::string& path)
	{
		// Internal helper — caller must hold registryMutex
		auto it = registry.find(path);
		if (it != registry.end()) {
			registry.erase(it);
		}
	}

	void AssetLifecycleManager::setReleaseCallback(ReleaseCallback callback)
	{
		std::scoped_lock lock(registryMutex);
		releaseCallback = std::move(callback);
	}

	void AssetLifecycleManager::setGracePeriod(float seconds)
	{
		std::scoped_lock lock(registryMutex);
		gracePeriodSeconds = seconds;
	}

	float AssetLifecycleManager::getGracePeriod() const
	{
		std::scoped_lock lock(registryMutex);
		return gracePeriodSeconds;
	}

	void AssetLifecycleManager::setMaxReleasesPerFrame(uint32_t max)
	{
		std::scoped_lock lock(registryMutex);
		maxReleasesPerFrame = max;
	}

	uint32_t AssetLifecycleManager::getMaxReleasesPerFrame() const
	{
		std::scoped_lock lock(registryMutex);
		return maxReleasesPerFrame;
	}

	AssetEntry AssetLifecycleManager::getAssetEntry(const std::string& path) const
	{
		std::scoped_lock lock(registryMutex);

		auto it = registry.find(path);
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
		for (const auto& [path, entry] : registry) {
			result.push_back(entry);
		}
		return result;
	}

	std::vector<AssetEntry> AssetLifecycleManager::getPendingReleases() const
	{
		std::scoped_lock lock(registryMutex);

		std::vector<AssetEntry> result;
		for (const auto& path : pendingReleaseQueue) {
			auto it = registry.find(path);
			if (it != registry.end() && it->second.state == AssetState::PendingRelease) {
				result.push_back(it->second);
			}
		}
		return result;
	}

	uint32_t AssetLifecycleManager::getRefCount(const std::string& path) const
	{
		std::scoped_lock lock(registryMutex);

		auto it = registry.find(path);
		if (it != registry.end()) {
			return it->second.refCount;
		}
		return 0;
	}

	bool AssetLifecycleManager::isTracked(const std::string& path) const
	{
		std::scoped_lock lock(registryMutex);
		return registry.contains(path);
	}

	void AssetLifecycleManager::clear()
	{
		std::scoped_lock lock(registryMutex);
		registry.clear();
		pendingReleaseQueue.clear();
		dependencies.clear();
	}

}
