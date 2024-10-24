#include "ResourceManager.hpp"
#include "TextureResource.hpp"
#include "AudioResource.hpp"
#include "MeshResource.hpp"


namespace resource {

	void ResourceManager::periodicCleanup()
	{
		using namespace std::chrono_literals;
		std::unique_lock lock(cacheMutex);

		while (running) {
			// Wait for 5 minutes or until notified to wake up early.
			cleanupCondition.wait_for(lock, 1min);

			// Check if we should stop running before proceeding.
			if (!running) {
				break;
			}

			// Perform the cleanup.
			unloadUnusedResources();
		}
	}

	void ResourceManager::unloadUnusedResources()
	{
		// Remove the entry if the resource is no longer referenced
		std::erase_if(textureCache, [](const auto& pair) {return pair.second.expired(); });
		std::erase_if(audioCache, [](const auto& pair) {return pair.second.expired(); });
		std::erase_if(meshCache, [](const auto& pair) {return pair.second.expired(); });
		std::erase_if(shaderCache, [](const auto& pair) { return pair.second.expired(); });
	}

	std::future<std::shared_ptr<TextureData>> ResourceManager::loadTextureAsync(std::string_view path)
	{
		return loadResourceAsync<TextureData>(
			path,
			textureCache,
			[](std::string_view p) { return TextureResource::loadTexture(p); });
	}

	std::future<std::shared_ptr<AudioData>> ResourceManager::loadAudioAsync(std::string_view path)
	{
		return loadResourceAsync<AudioData>(
			path,
			audioCache,
			[](std::string_view p) { return AudioResource::loadAudio(p); });
	}

	std::future<std::shared_ptr<MeshData>> ResourceManager::loadMeshAsync(std::string_view path)
	{
		return loadResourceAsync<MeshData>(
			path,
			meshCache,
			[](std::string_view p) { return MeshResource::loadMesh(p); });
	}

	std::future<std::shared_ptr<std::vector<ShaderModel>>> ResourceManager::loadShaderAsync(std::string_view path)
	{
		return loadResourceAsync<std::vector<ShaderModel>>(
			path,
			shaderCache,
			[](std::string_view p) { return ShaderResource::readShaderFile(p); });
	}

	void ResourceManager::init()
	{
		running = true;
		cleanupThread = std::jthread(&ResourceManager::periodicCleanup);
	}

	void ResourceManager::cleanUp()
	{
		notifyThread();
		cleanupCondition.notify_one(); // Notify the cleanup thread to wake up and exit.
		releaseResources();
	}

	void ResourceManager::notifyThread()
	{
		std::scoped_lock lock(cacheMutex);
		running = false;
	}

	void ResourceManager::releaseResources()
	{
		std::scoped_lock lock(cacheMutex);
		unloadUnusedResources();
		textureCache.clear();
		audioCache.clear();
		meshCache.clear();
	}

}
