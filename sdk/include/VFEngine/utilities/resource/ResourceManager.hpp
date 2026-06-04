#pragma once
#include <memory>
#include "../print/Log.hpp"
#include "../asset/AssetRef.hpp"
#include <unordered_map>
#include <string>
#include <future>
#include <mutex>
#include <chrono>
#include <thread>

#include <filesystem>
namespace fs = std::filesystem;

#include "../material/MaterialTypes.hpp"
#include "../material/MaterialInstanceTypes.hpp"
#include "../animator/AnimatorTypes.hpp"
#include "../terrain/TerrainMaterialTypes.hpp"
#include "Types.hpp"
#include "ShaderResource.hpp"
#include "MeshStreamHandle.hpp"
#include "AssetLifecycleManager.hpp"
#include "ResourceLoadTypes.hpp"
#include "CancellationToken.hpp"
#include "ResourceLoadScheduler.hpp"

namespace resource {
	template <typename Key, typename T, typename Hash = std::hash<Key>>
	using PendingLoadMap = std::unordered_map<Key, std::shared_future<std::shared_ptr<T>>, Hash>;

	class ResourceManager
	{
	private:
		inline static std::unordered_map<asset::AssetGUID, std::weak_ptr<TextureData>, asset::AssetGUID::Hash> textureCache;
		inline static std::unordered_map<asset::AssetGUID, std::weak_ptr<HDRData>, asset::AssetGUID::Hash> hdrCache;
		inline static std::unordered_map<asset::AssetGUID, std::weak_ptr<AudioData>, asset::AssetGUID::Hash> audioCache;
		inline static std::unordered_map<asset::AssetGUID, std::weak_ptr<MeshesData>, asset::AssetGUID::Hash> meshCache;
		inline static std::unordered_map<std::string, std::weak_ptr<std::vector<ShaderModel>>> shaderCache;
		inline static std::unordered_map<asset::AssetGUID, std::weak_ptr<material::MaterialData>, asset::AssetGUID::Hash> materialCache;
		inline static std::unordered_map<asset::AssetGUID, std::weak_ptr<material::MaterialInstanceData>, asset::AssetGUID::Hash> materialInstanceCache;
		inline static std::unordered_map<asset::AssetGUID, std::weak_ptr<FontData>, asset::AssetGUID::Hash> fontCache;
		inline static std::unordered_map<asset::AssetGUID, std::weak_ptr<AnimationData>, asset::AssetGUID::Hash> animationCache;
		inline static std::unordered_map<asset::AssetGUID, std::weak_ptr<animator::AnimatorData>, asset::AssetGUID::Hash> animatorCache;
		inline static std::unordered_map<asset::AssetGUID, std::weak_ptr<terrain::TerrainMaterialData>, asset::AssetGUID::Hash> terrainMaterialCache;

		// In-flight async loads — second caller for the same resource subscribes
		// to the existing shared_future instead of launching a duplicate load.
		inline static PendingLoadMap<asset::AssetGUID, TextureData, asset::AssetGUID::Hash> pendingTextureLoads;
		inline static PendingLoadMap<asset::AssetGUID, HDRData, asset::AssetGUID::Hash> pendingHDRLoads;
		inline static PendingLoadMap<asset::AssetGUID, AudioData, asset::AssetGUID::Hash> pendingAudioLoads;
		inline static PendingLoadMap<asset::AssetGUID, MeshesData, asset::AssetGUID::Hash> pendingMeshLoads;
		inline static PendingLoadMap<asset::AssetGUID, FontData, asset::AssetGUID::Hash> pendingFontLoads;
		inline static PendingLoadMap<asset::AssetGUID, AnimationData, asset::AssetGUID::Hash> pendingAnimationLoads;
		inline static PendingLoadMap<std::string, std::vector<ShaderModel>> pendingShaderLoads;

		inline static std::mutex cacheMutex;
		inline static std::jthread cleanupThread;
		inline static std::condition_variable cleanupCondition;
		inline static std::atomic<bool> running;
		inline static std::atomic<bool> shuttingDown{ false };
		inline static std::atomic<int32_t> pendingAsyncOps{ 0 };

	public:
		static FileType readHeaderFile(const fs::path& filePath);

		// Asset-based loading (GUID-keyed)
		static std::future<std::shared_ptr<TextureData>> loadTextureAsync(const asset::AssetRef& ref, const LoadHint& hint = {}, CancellationToken::Ptr cancellation = nullptr);
		static std::future<std::shared_ptr<HDRData>> loadHDRAsync(const asset::AssetRef& ref, const LoadHint& hint = {}, CancellationToken::Ptr cancellation = nullptr);
		static std::future<std::shared_ptr<AudioData>> loadAudioAsync(const asset::AssetRef& ref, const LoadHint& hint = {}, CancellationToken::Ptr cancellation = nullptr);
		static std::future<std::shared_ptr<MeshesData>> loadMeshAsync(const asset::AssetRef& ref, const LoadHint& hint = {}, CancellationToken::Ptr cancellation = nullptr);
		static std::future<std::shared_ptr<FontData>> loadFontAsync(const asset::AssetRef& ref, const LoadHint& hint = {}, CancellationToken::Ptr cancellation = nullptr);
		static std::future<std::shared_ptr<AnimationData>> loadAnimationAsync(const asset::AssetRef& ref, const LoadHint& hint = {}, CancellationToken::Ptr cancellation = nullptr);

		// Shader loading (path-based, internal engine resources)
		static std::future<std::shared_ptr<std::vector<ShaderModel>>> loadShaderAsync(std::string_view path);

		// Synchronous asset loading
		static std::shared_ptr<material::MaterialData> loadMaterial(const asset::AssetRef& ref);
		static std::shared_ptr<material::MaterialInstanceData> loadMaterialInstance(const asset::AssetRef& ref);
		static std::shared_ptr<animator::AnimatorData> loadAnimator(const asset::AssetRef& ref);
		static std::shared_ptr<terrain::TerrainMaterialData> loadTerrainMaterial(const asset::AssetRef& ref);

		// Invalidate cache entries (for reload support)
		static void invalidateMaterialCache(const asset::AssetRef& ref);
		static void invalidateMaterialInstanceCache(const asset::AssetRef& ref);
		static void invalidateTerrainMaterialCache(const asset::AssetRef& ref);

		static void init();
		static void cleanUp();

	private:
		static void periodicCleanup();
		static void unloadUnusedResources();
		static void notifyThread();
		static void releaseResources();

		template <typename T, typename LoaderFunc, typename MemoryEstimator = std::nullptr_t>
		static std::future<std::shared_ptr<T>> loadResourceAsync(
			const asset::AssetRef& ref,
			std::unordered_map<asset::AssetGUID, std::weak_ptr<T>, asset::AssetGUID::Hash>& cache,
			PendingLoadMap<asset::AssetGUID, T, asset::AssetGUID::Hash>& pendingLoads,
			LoaderFunc loader,
			AssetType assetType = AssetType::COUNT,
			MemoryEstimator memEstimator = nullptr,
			const LoadHint& hint = {},
			CancellationToken::Ptr cancellation = nullptr);

		template <typename T>
		static std::future<T> make_ready_future(T value) {
			std::promise<T> promise;
			promise.set_value(std::move(value));
			return promise.get_future();
		}
	};

	template<typename T, typename LoaderFunc, typename MemoryEstimator>
	inline std::future<std::shared_ptr<T>> ResourceManager::loadResourceAsync(
		const asset::AssetRef& ref,
		std::unordered_map<asset::AssetGUID, std::weak_ptr<T>, asset::AssetGUID::Hash>& cache,
		PendingLoadMap<asset::AssetGUID, T, asset::AssetGUID::Hash>& pendingLoads,
		LoaderFunc loader, AssetType assetType, MemoryEstimator memEstimator,
		const LoadHint& hint, CancellationToken::Ptr cancellation)
	{
		auto guid = ref.getGUID();
		if (!guid.isValid()) {
			vfLogError("Invalid AssetRef provided for resource loading");
			return make_ready_future(std::shared_ptr<T>(nullptr));
		}

		{
			std::scoped_lock lock(cacheMutex);

			// Check completed cache first
			auto cacheIt = cache.find(guid);
			if (cacheIt != cache.end()) {
				if (auto resource = cacheIt->second.lock()) {
					return make_ready_future(resource);
				}
			}

			// Subscribe to an already in-flight load instead of launching a duplicate
			auto pendingIt = pendingLoads.find(guid);
			if (pendingIt != pendingLoads.end()) {
				return std::async(std::launch::deferred, [sf = pendingIt->second]() mutable {
					return sf.get();
				});
			}
		}

		std::string path = ref.resolve();
		if (path.empty()) {
			vfLogError("Failed to resolve AssetRef GUID: {}", guid.toString());
			return make_ready_future(std::shared_ptr<T>(nullptr));
		}

		if (!cancellation)
			cancellation = CancellationToken::create();

		// Create a shared promise/future pair for the result
		auto sharedPromise = std::make_shared<std::promise<std::shared_ptr<T>>>();
		auto resultFuture = sharedPromise->get_future();

		// Register a shared_future in pendingLoads for deduplication
		auto sharedResultPromise = std::make_shared<std::promise<std::shared_ptr<T>>>();
		std::shared_future<std::shared_ptr<T>> sharedFuture = sharedResultPromise->get_future().share();
		{
			std::scoped_lock lock(cacheMutex);
			pendingLoads[guid] = sharedFuture;
		}

		pendingAsyncOps.fetch_add(1, std::memory_order_relaxed);

		// Package the I/O work as a LoadRequest for the scheduler
		LoadRequest request;
		request.guid = guid;
		request.hint = hint;
		request.cancellation = cancellation;
		request.computedPriority = ResourceLoadScheduler::computePriority(hint, {0.0f, 0.0f, 0.0f});
		request.executeLoad = [path = std::move(path), guid, loader, &cache, &pendingLoads,
			assetType, memEstimator, sharedPromise, sharedResultPromise, cancel = cancellation]() mutable {
			struct AsyncGuard { ~AsyncGuard() { pendingAsyncOps.fetch_sub(1, std::memory_order_release); } } guard;
			try {
				if (shuttingDown.load(std::memory_order_acquire) || (cancel && cancel->isCancelled())) {
					std::scoped_lock lock(cacheMutex);
					pendingLoads.erase(guid);
					sharedResultPromise->set_value(nullptr);
					sharedPromise->set_value(nullptr);
					return;
				}

				auto resource = std::make_shared<T>(loader(path));

				{
					std::scoped_lock lock(cacheMutex);
					pendingLoads.erase(guid);
					if (resource && !shuttingDown.load(std::memory_order_acquire)) {
						cache[guid] = resource;
						if (assetType != AssetType::COUNT) {
							size_t memBytes = 0;
							if constexpr (!std::is_null_pointer_v<MemoryEstimator>) {
								memBytes = memEstimator(*resource);
							}
							AssetLifecycleManager::instance().acquire(guid, assetType, memBytes);
						}
					}
				}

				sharedResultPromise->set_value(resource);
				sharedPromise->set_value(resource);
			}
			catch (const std::exception& e) {
				std::scoped_lock lock(cacheMutex);
				pendingLoads.erase(guid);
				vfLogError("Exception loading resource '{}': {}", path, e.what());
				sharedResultPromise->set_value(nullptr);
				sharedPromise->set_value(nullptr);
			}
			catch (...) {
				std::scoped_lock lock(cacheMutex);
				pendingLoads.erase(guid);
				vfLogError("Unknown exception loading resource: {}", path);
				sharedResultPromise->set_value(nullptr);
				sharedPromise->set_value(nullptr);
			}
		};

		ResourceLoadScheduler::instance().submit(std::move(request));

		return resultFuture;
	}
}
