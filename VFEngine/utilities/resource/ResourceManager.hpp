#pragma once
#include <memory>
#include <unordered_map>
#include <string>
#include <future>
#include <mutex>
#include <chrono>
#include <thread>

#include <filesystem>
namespace fs = std::filesystem;

#include "../print/EditorLogger.hpp"
#include "../material/MaterialTypes.hpp"
#include "Types.hpp"
#include "ShaderResource.hpp"
#include "MeshStreamHandle.hpp"

namespace resource {
	class ResourceManager
	{
	private:
		inline static std::unordered_map<std::string, std::weak_ptr<TextureData>> textureCache;
		inline static std::unordered_map<std::string, std::weak_ptr<HDRData>> hdrCache;
		inline static std::unordered_map<std::string, std::weak_ptr<AudioData>> audioCache;
		inline static std::unordered_map<std::string, std::weak_ptr<MeshesData>> meshCache;
		inline static std::unordered_map<std::string, std::weak_ptr<std::vector<ShaderModel>>> shaderCache;
		inline static std::unordered_map<std::string, std::weak_ptr<material::MaterialData>> materialCache;

		inline static std::mutex cacheMutex;
		inline static std::jthread cleanupThread;
		inline static std::condition_variable cleanupCondition;
		inline static std::atomic<bool> running;

	public:
		static FileType readHeaderFile(const fs::path& filePath);
		static std::future <std::shared_ptr<TextureData>> loadTextureAsync(std::string_view path);
		static std::future <std::shared_ptr<HDRData>> loadHDRAsync(std::string_view path);
		static std::future <std::shared_ptr<AudioData>> loadAudioAsync(std::string_view path);
		static std::future <std::shared_ptr<MeshesData>> loadMeshAsync(std::string_view path);
		static std::future <std::shared_ptr<std::vector<ShaderModel>>> loadShaderAsync(std::string_view path);
		static std::future <std::shared_ptr<material::MaterialData>> loadMaterialAsync(std::string_view path);

		static std::shared_ptr<material::MaterialData> loadMaterial(std::string_view path);

		// Mesh streaming support
		static std::unique_ptr<MeshStreamHandle> openMeshStream(std::string_view path);
		
		static std::shared_ptr<material::MaterialData> getMaterial(std::string_view path);

		// Invalidate material cache entry (for reload support)
		static void invalidateMaterialCache(std::string_view path);

		static void init();
		static void cleanUp();

	private:
		static void periodicCleanup();
		static void unloadUnusedResources();
		static void notifyThread();
		static void releaseResources();

		template <typename T, typename LoaderFunc>
		static std::future<std::shared_ptr<T>> loadResourceAsync(
			std::string_view path,
			std::unordered_map<std::string, std::weak_ptr<T>>& cache,
			LoaderFunc loader);

		template <typename T>
		static std::future<T> make_ready_future(T value) {
			std::promise<T> promise;
			promise.set_value(std::move(value));
			return promise.get_future();
		}
	};

	template<typename T, typename LoaderFunc>
	inline std::future<std::shared_ptr<T>> ResourceManager::loadResourceAsync(std::string_view path, std::unordered_map<std::string, std::weak_ptr<T>>& cache, LoaderFunc loader)
	{
		if (auto resource = cache[path.data()].lock()) {
			return make_ready_future(resource);
		}

		return std::async(std::launch::async, [path = std::string(path), loader, &cache]() -> std::shared_ptr<T> {
			try {
				// Validate path before processing
				if (path.empty()) {
					vfLogError("Empty path provided for resource loading");
					return nullptr;
				}
				
				auto resource = std::make_shared<T>(loader(path));
				
				// Only cache if resource was successfully loaded
				if (resource) {
					std::scoped_lock lock(cacheMutex);
					cache[path] = resource;
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


