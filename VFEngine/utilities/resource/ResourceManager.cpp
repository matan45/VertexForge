#include "ResourceManager.hpp"
#include "../print/Log.hpp"
#include "TextureResource.hpp"
#include "AudioResource.hpp"
#include "FontResource.hpp"
#include "AnimationResource.hpp"
#include "MeshStreamHandle.hpp"
#include "../material/MaterialAsset.hpp"
#include "../material/MaterialInstanceAsset.hpp"
#include "../animator/AnimatorAsset.hpp"
#include "../terrain/TerrainMaterialAsset.hpp"
#include <bit>
#include <algorithm>
#include <cctype>
#include <fstream>


namespace resource
{
    void ResourceManager::periodicCleanup()
    {
        using namespace std::chrono_literals;
        std::unique_lock lock(cacheMutex);

        while (running)
        {
            cleanupCondition.wait_for(lock, 1min);

            if (!running)
                break;

            unloadUnusedResources();
        }
    }

    void ResourceManager::unloadUnusedResources()
    {
        auto& lifecycle = AssetLifecycleManager::instance();

        auto releaseExpired = [&lifecycle](auto& cache) {
            std::erase_if(cache, [&lifecycle](const auto& pair) {
                if (pair.second.expired()) {
                    lifecycle.release(pair.first);
                    return true;
                }
                return false;
            });
        };

        releaseExpired(textureCache);
        releaseExpired(hdrCache);
        releaseExpired(audioCache);
        releaseExpired(meshCache);
        releaseExpired(materialCache);
        releaseExpired(materialInstanceCache);
        releaseExpired(fontCache);
        releaseExpired(animationCache);
        releaseExpired(animatorCache);
        releaseExpired(terrainMaterialCache);

        // Shaders are engine-internal, not lifecycle-tracked — just clean expired entries
        std::erase_if(shaderCache, [](const auto& pair) { return pair.second.expired(); });
    }

    static FileType getExpectedTypeFromExtension(const std::string& ext)
    {
        if (ext == ".vfimage") return FileType::TEXTURE;
        if (ext == ".vfmesh") return FileType::MESH;
        if (ext == ".vfhdr") return FileType::HDR;
        if (ext == ".vfaudio") return FileType::AUDIO;
        if (ext == ".vfanim") return FileType::ANIMATION;
        if (ext == ".vffont") return FileType::FONT;
        if (ext == ".vfterrain") return FileType::TERRAIN;
        if (ext == ".vflightmap") return FileType::LIGHTMAP;
        return FileType::UNKNOWN;
    }

    static const char* getFileTypeName(FileType type)
    {
        switch (type) {
            case FileType::TEXTURE: return "TEXTURE";
            case FileType::MESH: return "MESH";
            case FileType::HDR: return "HDR";
            case FileType::AUDIO: return "AUDIO";
            case FileType::ANIMATION: return "ANIMATION";
            case FileType::SCENE: return "SCENE";
            case FileType::FONT: return "FONT";
            case FileType::SKELETON: return "SKELETON";
            case FileType::ANIMATOR: return "ANIMATOR";
            case FileType::TERRAIN: return "TERRAIN";
            case FileType::LIGHTMAP: return "LIGHTMAP";
            default: return "UNKNOWN";
        }
    }

    static bool isValidFileType(uint8_t typeByte)
    {
        switch (static_cast<FileType>(typeByte)) {
            case FileType::TEXTURE:
            case FileType::MESH:
            case FileType::ANIMATION:
            case FileType::HDR:
            case FileType::AUDIO:
            case FileType::SCENE:
            case FileType::FONT:
            case FileType::SKELETON:
            case FileType::ANIMATOR:
            case FileType::TERRAIN:
            case FileType::LIGHTMAP:
                return true;
            default:
                return false;
        }
    }

    FileType ResourceManager::readHeaderFile(const fs::path& filePath)
    {
        if (filePath.empty())
        {
            vfLogError("Empty file path provided");
            return FileType::UNKNOWN;
        }

        std::error_code ec;
        if (!fs::exists(filePath, ec) || ec)
        {
            vfLogError("File does not exist: {}", filePath.string());
            return FileType::UNKNOWN;
        }

        auto extension = filePath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

        // Handle text-based formats by extension
        if (extension == ".vfscene")
        {
            return FileType::SCENE;
        }

        // For binary formats, read the header
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("Failed to open file: {}", filePath.string());
            return FileType::UNKNOWN;
        }

        uint8_t typeByte = 0;
        file.read(reinterpret_cast<char*>(&typeByte), sizeof(typeByte));

        if (!file || file.gcount() != sizeof(typeByte))
        {
            vfLogError("Failed to read header from file: {}", filePath.string());
            return FileType::UNKNOWN;
        }

        if (!isValidFileType(typeByte))
        {
            vfLogError("Invalid file type header {} in file: {}", typeByte, filePath.string());
            return FileType::UNKNOWN;
        }

        FileType headerType = static_cast<FileType>(typeByte);

        FileType expectedType = getExpectedTypeFromExtension(extension);
        if (expectedType != FileType::UNKNOWN && headerType != expectedType)
        {
            vfLogWarning("File header/extension mismatch: {} has header {} but extension expects {} - using extension type",
                       filePath.string(), getFileTypeName(headerType), getFileTypeName(expectedType));
            return expectedType;
        }

        return headerType;
    }

    std::future<std::shared_ptr<TextureData>> ResourceManager::loadTextureAsync(std::string_view path)
    {
        return loadResourceAsync<TextureData>(
            path,
            textureCache,
            [](std::string_view p) { return TextureResource::loadTexture(p); },
            AssetType::Texture);
    }

    std::future<std::shared_ptr<HDRData>> ResourceManager::loadHDRAsync(std::string_view path)
    {
        return loadResourceAsync<HDRData>(
            path,
            hdrCache,
            [](std::string_view p) { return TextureResource::loadHDR(p); },
            AssetType::HDR);
    }

    std::future<std::shared_ptr<AudioData>> ResourceManager::loadAudioAsync(std::string_view path)
    {
        return loadResourceAsync<AudioData>(
            path,
            audioCache,
            [](std::string_view p) { return AudioResource::loadAudio(p); },
            AssetType::Audio);
    }

    std::future<std::shared_ptr<MeshesData>> ResourceManager::loadMeshAsync(std::string_view path)
    {
        return loadResourceAsync<MeshesData>(
            path,
            meshCache,
            [](std::string_view p) {
                return MeshStreamResource::loadAll(p);
            },
            AssetType::Mesh);
    }

    std::future<std::shared_ptr<std::vector<ShaderModel>>> ResourceManager::loadShaderAsync(std::string_view path)
    {
        return loadResourceAsync<std::vector<ShaderModel>>(
            path,
            shaderCache,
            [](std::string_view p) { return ShaderResource::readShaderFile(p); });
    }

    std::future<std::shared_ptr<FontData>> ResourceManager::loadFontAsync(std::string_view path)
    {
        return loadResourceAsync<FontData>(
            path,
            fontCache,
            [](std::string_view p) { return FontResource::loadFont(p); },
            AssetType::Font);
    }

    std::future<std::shared_ptr<AnimationData>> ResourceManager::loadAnimationAsync(std::string_view path)
    {
        return loadResourceAsync<AnimationData>(
            path,
            animationCache,
            [](std::string_view p) { return AnimationResource::loadAnimation(p); },
            AssetType::Animation);
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
        hdrCache.clear();
        audioCache.clear();
        meshCache.clear();
        shaderCache.clear();
        materialCache.clear();
        materialInstanceCache.clear();
        fontCache.clear();
        animationCache.clear();
        animatorCache.clear();
        terrainMaterialCache.clear();

        AssetLifecycleManager::instance().clear();

        vfLogInfo("All resource caches cleared");
    }

    std::shared_ptr<material::MaterialData> ResourceManager::loadMaterial(std::string_view path)
    {
        {
            std::scoped_lock lock(cacheMutex);
            auto it = materialCache.find(std::string(path));
            if (it != materialCache.end()) {
                if (auto existing = it->second.lock()) {
                    AssetLifecycleManager::instance().acquire(std::string(path), AssetType::Material);
                    return existing;
                }
            }
        }

        auto result = material::MaterialAsset::load(path);
        if (!result) {
            vfLogError("Failed to load material: {}", path);
            return nullptr;
        }

        auto material = std::make_shared<material::MaterialData>(std::move(*result));

        {
            std::scoped_lock lock(cacheMutex);
            materialCache[std::string(path)] = material;
        }

        auto& lifecycle = AssetLifecycleManager::instance();
        lifecycle.acquire(std::string(path), AssetType::Material);

        // Register texture dependencies from shader graph
        for (const auto& node : material->graph.nodes) {
            if (node.type == material::NodeType::TextureSample ||
                node.type == material::NodeType::OrmSample) {
                auto propIt = node.properties.find("texturePath");
                if (propIt != node.properties.end() &&
                    std::holds_alternative<std::string>(propIt->second)) {
                    const auto& texPath = std::get<std::string>(propIt->second);
                    if (!texPath.empty()) {
                        lifecycle.addDependency(std::string(path), texPath, AssetType::Texture);
                    }
                }
            }
        }

        return material;
    }

    void ResourceManager::invalidateMaterialCache(std::string_view path)
    {
        std::scoped_lock lock(cacheMutex);
        materialCache.erase(std::string(path));
    }

    std::shared_ptr<material::MaterialInstanceData> ResourceManager::loadMaterialInstance(std::string_view path)
    {
        {
            std::scoped_lock lock(cacheMutex);
            auto it = materialInstanceCache.find(std::string(path));
            if (it != materialInstanceCache.end()) {
                if (auto existing = it->second.lock()) {
                    AssetLifecycleManager::instance().acquire(std::string(path), AssetType::MaterialInstance);
                    return existing;
                }
            }
        }

        auto result = material::MaterialInstanceAsset::load(path);
        if (!result) {
            vfLogError("Failed to load material instance: {}", path);
            return nullptr;
        }

        auto instance = std::make_shared<material::MaterialInstanceData>(std::move(*result));

        {
            std::scoped_lock lock(cacheMutex);
            materialInstanceCache[std::string(path)] = instance;
        }

        auto& lifecycle = AssetLifecycleManager::instance();
        lifecycle.acquire(std::string(path), AssetType::MaterialInstance);

        // Register parent material dependency
        if (!instance->parentMaterialPath.empty()) {
            lifecycle.addDependency(std::string(path), instance->parentMaterialPath, AssetType::Material);
        }
        // Register texture override dependencies
        for (const auto& [slot, texPath] : instance->textureOverrides) {
            if (!texPath.empty()) {
                lifecycle.addDependency(std::string(path), texPath, AssetType::Texture);
            }
        }

        return instance;
    }

    void ResourceManager::invalidateMaterialInstanceCache(std::string_view path)
    {
        std::scoped_lock lock(cacheMutex);
        materialInstanceCache.erase(std::string(path));
    }

    std::shared_ptr<terrain::TerrainMaterialData> ResourceManager::loadTerrainMaterial(std::string_view path)
    {
        {
            std::scoped_lock lock(cacheMutex);
            auto it = terrainMaterialCache.find(std::string(path));
            if (it != terrainMaterialCache.end()) {
                if (auto existing = it->second.lock()) {
                    return existing;
                }
            }
        }

        auto result = terrain::TerrainMaterialAsset::load(path);
        if (!result) {
            vfLogError("Failed to load terrain material: {}", path);
            return nullptr;
        }

        auto terrainMat = std::make_shared<terrain::TerrainMaterialData>(std::move(*result));

        {
            std::scoped_lock lock(cacheMutex);
            terrainMaterialCache[std::string(path)] = terrainMat;
        }

        return terrainMat;
    }

    void ResourceManager::invalidateTerrainMaterialCache(std::string_view path)
    {
        std::scoped_lock lock(cacheMutex);
        terrainMaterialCache.erase(std::string(path));
    }

    void ResourceManager::migrateCache(const std::string& oldPath, const std::string& newPath)
    {
        std::scoped_lock lock(cacheMutex);

        auto migrate = [&](auto& cache) {
            auto it = cache.find(oldPath);
            if (it != cache.end())
            {
                cache[newPath] = std::move(it->second);
                cache.erase(it);
            }
        };

        migrate(textureCache);
        migrate(hdrCache);
        migrate(audioCache);
        migrate(meshCache);
        migrate(shaderCache);
        migrate(materialCache);
        migrate(materialInstanceCache);
        migrate(fontCache);
        migrate(animationCache);
        migrate(animatorCache);
        migrate(terrainMaterialCache);
    }

    void ResourceManager::removeCacheEntry(const std::string& path)
    {
        std::scoped_lock lock(cacheMutex);

        textureCache.erase(path);
        hdrCache.erase(path);
        audioCache.erase(path);
        meshCache.erase(path);
        shaderCache.erase(path);
        materialCache.erase(path);
        materialInstanceCache.erase(path);
        fontCache.erase(path);
        animationCache.erase(path);
        animatorCache.erase(path);
        terrainMaterialCache.erase(path);

        AssetLifecycleManager::instance().forceRelease(path);
    }

    void ResourceManager::migrateCachePrefix(const std::string& oldPrefix, const std::string& newPrefix)
    {
        std::scoped_lock lock(cacheMutex);

        auto migratePrefix = [&](auto& cache) {
            std::vector<std::pair<std::string, typename std::remove_reference_t<decltype(cache)>::mapped_type>> toInsert;
            std::vector<std::string> toErase;

            for (auto& [key, value] : cache)
            {
                if (key.starts_with(oldPrefix))
                {
                    std::string newKey = newPrefix + key.substr(oldPrefix.size());
                    toInsert.emplace_back(std::move(newKey), std::move(value));
                    toErase.push_back(key);
                }
            }

            for (const auto& key : toErase)
            {
                cache.erase(key);
            }
            for (auto& [key, value] : toInsert)
            {
                cache[std::move(key)] = std::move(value);
            }
        };

        migratePrefix(textureCache);
        migratePrefix(hdrCache);
        migratePrefix(audioCache);
        migratePrefix(meshCache);
        migratePrefix(shaderCache);
        migratePrefix(materialCache);
        migratePrefix(materialInstanceCache);
        migratePrefix(fontCache);
        migratePrefix(animationCache);
        migratePrefix(animatorCache);
        migratePrefix(terrainMaterialCache);
    }

    std::shared_ptr<animator::AnimatorData> ResourceManager::loadAnimator(std::string_view path)
    {
        {
            std::scoped_lock lock(cacheMutex);
            auto it = animatorCache.find(std::string(path));
            if (it != animatorCache.end()) {
                if (auto existing = it->second.lock()) {
                    AssetLifecycleManager::instance().acquire(std::string(path), AssetType::Animator);
                    return existing;
                }
            }
        }

        auto result = animator::AnimatorAsset::load(path);
        if (!result) {
            vfLogError("Failed to load animator: {}", path);
            return nullptr;
        }

        auto animatorData = std::make_shared<animator::AnimatorData>(std::move(*result));

        {
            std::scoped_lock lock(cacheMutex);
            animatorCache[std::string(path)] = animatorData;
        }

        auto& lifecycle = AssetLifecycleManager::instance();
        lifecycle.acquire(std::string(path), AssetType::Animator);

        // Register animation clip dependencies
        for (const auto& state : animatorData->graph.states) {
            if (!state.animationPath.empty()) {
                lifecycle.addDependency(std::string(path), state.animationPath, AssetType::Animation);
            }
        }

        return animatorData;
    }
}
