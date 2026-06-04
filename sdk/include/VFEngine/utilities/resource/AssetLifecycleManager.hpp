#pragma once
#include "AssetTypes.hpp"
#include "../asset/AssetGUID.hpp"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <string>
#include <functional>

namespace resource {

	using ReleaseCallback = std::function<void(const asset::AssetGUID& guid, AssetType type)>;

	class AssetLifecycleManager
	{
	public:
		static AssetLifecycleManager& instance();

		AssetLifecycleManager(const AssetLifecycleManager&) = delete;
		AssetLifecycleManager& operator=(const AssetLifecycleManager&) = delete;

		void acquire(const asset::AssetGUID& guid, AssetType type, size_t estimatedMemoryBytes = 0);
		void release(const asset::AssetGUID& guid);
		void tick(float deltaTime);

		void forceRelease(const asset::AssetGUID& guid);

		void addDependency(const asset::AssetGUID& parent, const asset::AssetGUID& child, AssetType childType);
		void removeDependencies(const asset::AssetGUID& parent);

		void setReleaseCallback(ReleaseCallback callback);

		AssetEntry getAssetEntry(const asset::AssetGUID& guid) const;
		std::vector<AssetEntry> getAllAssets() const;
		std::vector<AssetEntry> getPendingReleases() const;

		bool isTracked(const asset::AssetGUID& guid) const;

		void clear();

	private:
		AssetLifecycleManager() = default;
		~AssetLifecycleManager() = default;

		struct DependencyInfo {
			asset::AssetGUID child;
			AssetType childType;
		};

		std::unordered_map<asset::AssetGUID, AssetEntry, asset::AssetGUID::Hash> registry;
		std::vector<asset::AssetGUID> pendingReleaseQueue;
		std::unordered_map<asset::AssetGUID, std::vector<DependencyInfo>, asset::AssetGUID::Hash> dependencies;

		ReleaseCallback releaseCallback;

		float gracePeriodSeconds = 5.0f;
		uint32_t maxReleasesPerFrame = 8;

		mutable std::mutex registryMutex;
	};

}
