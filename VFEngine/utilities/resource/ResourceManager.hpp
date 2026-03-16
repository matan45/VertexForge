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

namespace resource {
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

		inline static std::mutex cacheMutex;
		inline static std::jthread cleanupThread;
		inline static std::condition_variable cleanupCondition;
		inline static std::atomic<bool> running;
		inline static std::atomic<bool> shuttingDown{ false };
		inline static std::atomic<uint32_t> pendingAsyncOps{ 0 };

	public:
		static FileType readHeaderFile(const fs::path& filePath);

		// Asset-based loading (GUID-keyed)
		static std::future<std::shared_ptr<TextureData>> loadTextureAsync(const asset::AssetRef& ref);
		static std::future<std::shared_ptr<HDRData>> loadHDRAsync(const asset::AssetRef& ref);
		static std::future<std::shared_ptr<AudioData>> loadAudioAsync(const asset::AssetRef& ref);
		static std::future<std::shared_ptr<MeshesData>> loadMeshAsync(const asset::AssetRef& ref);
		static std::future<std::shared_ptr<FontData>> loadFontAsync(const asset::AssetRef& ref);
		static std::future<std::shared_ptr<AnimationData>> loadAnimationAsync(const asset::AssetRef& ref);

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
			LoaderFunc loader,
			AssetType assetType = AssetType::COUNT,
			MemoryEstimator memEstimator = nullptr);

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
		LoaderFunc loader, AssetType assetType, MemoryEstimator memEstimator)
	{
		auto guid = ref.getGUID();
		if (!guid.isValid()) {
			vfLogError("Invalid AssetRef provided for resource loading");
			return make_ready_future(std::shared_ptr<T>(nullptr));
		}

		{
			std::scoped_lock lock(cacheMutex);
			auto it = cache.find(guid);
			if (it != cache.end()) {
				if (auto resource = it->second.lock()) {
					return make_ready_future(resource);
				}
			}
		}

		std::string path = ref.resolve();
		if (path.empty()) {
			vfLogError("Failed to resolve AssetRef GUID: {}", guid.toString());
			return make_ready_future(std::shared_ptr<T>(nullptr));
		}

		pendingAsyncOps.fetch_add(1, std::memory_order_relaxed);
		return std::async(std::launch::async, [path = std::move(path), guid, loader, &cache, assetType, memEstimator]() -> std::shared_ptr<T> {
			struct AsyncGuard { ~AsyncGuard() { pendingAsyncOps.fetch_sub(1, std::memory_order_release); } } guard;
			try {
				if (shuttingDown.load(std::memory_order_acquire)) {
					return nullptr;
				}

				auto resource = std::make_shared<T>(loader(path));

				if (resource && !shuttingDown.load(std::memory_order_acquire)) {
					std::scoped_lock lock(cacheMutex);
					cache[guid] = resource;
					if (assetType != AssetType::COUNT) {
						size_t memBytes = 0;
						if constexpr (!std::is_null_pointer_v<MemoryEstimator>) {
							memBytes = memEstimator(*resource);
						}
						AssetLifecycleManager::instance().acquire(guid, assetType, memBytes);
					}
				}

				return resource;
			}
			catch (const std::exception& e) {
				vfLogError("Exception loading resource '{}': {}", path, e.what());
				return nullptr;
			}
			catch (...) {
				vfLogError("Unknown exception loading resource: {}", path);
				return nullptr;
			}
			});
	}
}
