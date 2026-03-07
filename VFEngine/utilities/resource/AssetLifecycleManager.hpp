#pragma once
#include "AssetTypes.hpp"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <string>
#include <functional>

namespace resource {

	using ReleaseCallback = std::function<void(const std::string& path, AssetType type)>;

	class AssetLifecycleManager
	{
	public:
		static AssetLifecycleManager& instance();

		AssetLifecycleManager(const AssetLifecycleManager&) = delete;
		AssetLifecycleManager& operator=(const AssetLifecycleManager&) = delete;

		void acquire(const std::string& path, AssetType type, size_t estimatedMemoryBytes = 0);
		void release(const std::string& path);
		void tick(float deltaTime);

		void forceRelease(const std::string& path);

		// Dependency tracking: when parentPath is released, childPath is also released.
		// The child gets an implicit acquire. Multiple parents can share the same child.
		void addDependency(const std::string& parentPath, const std::string& childPath, AssetType childType);
		void removeDependencies(const std::string& parentPath);

		void setReleaseCallback(ReleaseCallback callback);

		void setGracePeriod(float seconds);
		float getGracePeriod() const;

		void setMaxReleasesPerFrame(uint32_t max);
		uint32_t getMaxReleasesPerFrame() const;

		AssetEntry getAssetEntry(const std::string& path) const;
		std::vector<AssetEntry> getAllAssets() const;
		std::vector<AssetEntry> getPendingReleases() const;

		uint32_t getRefCount(const std::string& path) const;
		bool isTracked(const std::string& path) const;

		void clear();

	private:
		AssetLifecycleManager() = default;
		~AssetLifecycleManager() = default;

		void processRelease(const std::string& path);

		struct DependencyInfo {
			std::string childPath;
			AssetType childType;
		};

		std::unordered_map<std::string, AssetEntry> registry;
		std::vector<std::string> pendingReleaseQueue;
		std::unordered_map<std::string, std::vector<DependencyInfo>> dependencies;

		ReleaseCallback releaseCallback;

		float gracePeriodSeconds = 5.0f;
		uint32_t maxReleasesPerFrame = 8;

		mutable std::mutex registryMutex;
	};

}
