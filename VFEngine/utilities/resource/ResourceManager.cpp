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
        releaseExpired(fontCache);
        releaseExpired(animationCache);
        releaseExpired(animatorCache);

        // Material caches don't acquire in lifecycle (entity components own the lifecycle reference).
        // Just clean expired weak_ptrs without releasing from lifecycle.
        auto cleanExpired = [](auto& cache) {
            std::erase_if(cache, [](const auto& pair) { return pair.second.expired(); });
        };
        cleanExpired(materialCache);
        cleanExpired(materialInstanceCache);
        cleanExpired(terrainMaterialCache);

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

    std::future<std::shared_ptr<TextureData>> ResourceManager::loadTextureAsync(const asset::AssetRef& ref)
    {
        return loadResourceAsync<TextureData>(
            ref,
            textureCache,
            pendingTextureLoads,
            [](const std::string& p) { return TextureResource::loadTexture(p); },
            AssetType::Texture,
            [](const TextureData& tex) -> size_t {
                size_t total = 0;
                for (const auto& mip : tex.mipData) {
                    total += mip.data.size();
                }
                return total;
            });
    }

    std::future<std::shared_ptr<HDRData>> ResourceManager::loadHDRAsync(const asset::AssetRef& ref)
    {
        return loadResourceAsync<HDRData>(
            ref,
            hdrCache,
            pendingHDRLoads,
            [](const std::string& p) { return TextureResource::loadHDR(p); },
            AssetType::HDR,
            [](const HDRData& hdr) -> size_t {
                return hdr.getDataSize();
            });
    }

    std::future<std::shared_ptr<AudioData>> ResourceManager::loadAudioAsync(const asset::AssetRef& ref)
    {
        return loadResourceAsync<AudioData>(
            ref,
            audioCache,
            pendingAudioLoads,
            [](const std::string& p) { return AudioResource::loadAudio(p); },
            AssetType::Audio,
            [](const AudioData& audio) -> size_t {
                return audio.data.size() * sizeof(short);
            });
    }

    std::future<std::shared_ptr<MeshesData>> ResourceManager::loadMeshAsync(const asset::AssetRef& ref)
    {
        return loadResourceAsync<MeshesData>(
            ref,
            meshCache,
            pendingMeshLoads,
            [](const std::string& p) {
                return MeshStreamResource::loadAll(p);
            },
            AssetType::Mesh,
            [](const MeshesData& meshes) -> size_t {
                size_t total = 0;
                for (const auto& mesh : meshes.meshes) {
                    for (const auto& lod : mesh.lodLevels) {
                        total += lod.vertices.size() * sizeof(Vertex);
                        total += lod.indices.size() * sizeof(uint32_t);
                    }
                }
                return total;
            });
    }

    std::future<std::shared_ptr<std::vector<ShaderModel>>> ResourceManager::loadShaderAsync(std::string_view path)
    {
        std::string key(path);

        {
            std::scoped_lock lock(cacheMutex);
            auto cacheIt = shaderCache.find(key);
            if (cacheIt != shaderCache.end()) {
                if (auto resource = cacheIt->second.lock()) {
                    return make_ready_future(resource);
                }
            }

            auto pendingIt = pendingShaderLoads.find(key);
            if (pendingIt != pendingShaderLoads.end()) {
                return std::async(std::launch::deferred, [sf = pendingIt->second]() mutable {
                    return sf.get();
                });
            }
        }

        pendingAsyncOps.fetch_add(1, std::memory_order_relaxed);
        std::shared_future<std::shared_ptr<std::vector<ShaderModel>>> sharedFuture = std::async(std::launch::async,
            [key]() -> std::shared_ptr<std::vector<ShaderModel>> {
            struct AsyncGuard { ~AsyncGuard() { pendingAsyncOps.fetch_sub(1, std::memory_order_release); } } guard;
            try {
                if (shuttingDown.load(std::memory_order_acquire)) {
                    std::scoped_lock lock(cacheMutex);
                    pendingShaderLoads.erase(key);
                    return nullptr;
                }

                if (key.empty()) {
                    vfLogError("Empty path provided for shader loading");
                    std::scoped_lock lock(cacheMutex);
                    pendingShaderLoads.erase(key);
                    return nullptr;
                }

                auto resource = std::make_shared<std::vector<ShaderModel>>(ShaderResource::readShaderFile(key));

                {
                    std::scoped_lock lock(cacheMutex);
                    pendingShaderLoads.erase(key);
                    if (resource && !shuttingDown.load(std::memory_order_acquire)) {
                        shaderCache[key] = resource;
                    }
                }

                return resource;
            }
            catch (const std::exception& e) {
                std::scoped_lock lock(cacheMutex);
                pendingShaderLoads.erase(key);
                vfLogError("Exception loading shader '{}': {}", key, e.what());
                return nullptr;
            }
            catch (...) {
                std::scoped_lock lock(cacheMutex);
                pendingShaderLoads.erase(key);
                vfLogError("Unknown exception loading shader: {}", key);
                return nullptr;
            }
        }).share();

        {
            std::scoped_lock lock(cacheMutex);
            pendingShaderLoads[key] = sharedFuture;
        }

        return std::async(std::launch::deferred, [sf = std::move(sharedFuture)]() mutable {
            return sf.get();
        });
    }

    std::future<std::shared_ptr<FontData>> ResourceManager::loadFontAsync(const asset::AssetRef& ref)
    {
        return loadResourceAsync<FontData>(
            ref,
            fontCache,
            pendingFontLoads,
            [](const std::string& p) { return FontResource::loadFont(p); },
            AssetType::Font);
    }

    std::future<std::shared_ptr<AnimationData>> ResourceManager::loadAnimationAsync(const asset::AssetRef& ref)
    {
        return loadResourceAsync<AnimationData>(
            ref,
            animationCache,
            pendingAnimationLoads,
            [](const std::string& p) { return AnimationResource::loadAnimation(p); },
            AssetType::Animation);
    }

    void ResourceManager::init()
    {
        running = true;
        shuttingDown = false;
        pendingAsyncOps = 0;
        cleanupThread = std::jthread(&ResourceManager::periodicCleanup);
    }

    void ResourceManager::cleanUp()
    {
        shuttingDown.store(true, std::memory_order_release);

        // Wait for in-flight async operations to complete (with timeout)
        constexpr int maxWaitMs = 5000;
        int waited = 0;
        while (pendingAsyncOps.load(std::memory_order_acquire) > 0 && waited < maxWaitMs)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ++waited;
        }
        if (waited >= maxWaitMs)
        {
            vfLogWarning("ResourceManager::cleanUp() timed out waiting for {} pending async operations",
                         pendingAsyncOps.load(std::memory_order_relaxed));
        }

        notifyThread();
        cleanupCondition.notify_one();
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

        pendingTextureLoads.clear();
        pendingHDRLoads.clear();
        pendingAudioLoads.clear();
        pendingMeshLoads.clear();
        pendingFontLoads.clear();
        pendingAnimationLoads.clear();
        pendingShaderLoads.clear();

        AssetLifecycleManager::instance().clear();

        vfLogInfo("All resource caches cleared");
    }

    std::shared_ptr<material::MaterialData> ResourceManager::loadMaterial(const asset::AssetRef& ref)
    {
        if (!ref.isValid()) return nullptr;

        auto guid = ref.getGUID();
        {
            std::scoped_lock lock(cacheMutex);
            auto it = materialCache.find(guid);
            if (it != materialCache.end()) {
                if (auto existing = it->second.lock()) {
                    return existing;
                }
            }
        }

        std::string path = ref.resolve();
        if (path.empty()) {
            vfLogError("Failed to resolve material AssetRef: {}", guid.toString());
            return nullptr;
        }

        auto result = material::MaterialAsset::load(path);
        if (!result) {
            vfLogError("Failed to load material: {}", path);
            return nullptr;
        }

        auto material = std::make_shared<material::MaterialData>(std::move(*result));

        {
            std::scoped_lock lock(cacheMutex);
            materialCache[guid] = material;
        }

        return material;
    }

    void ResourceManager::invalidateMaterialCache(const asset::AssetRef& ref)
    {
        std::scoped_lock lock(cacheMutex);
        materialCache.erase(ref.getGUID());
    }

    std::shared_ptr<material::MaterialInstanceData> ResourceManager::loadMaterialInstance(const asset::AssetRef& ref)
    {
        if (!ref.isValid()) return nullptr;

        auto guid = ref.getGUID();
        {
            std::scoped_lock lock(cacheMutex);
            auto it = materialInstanceCache.find(guid);
            if (it != materialInstanceCache.end()) {
                if (auto existing = it->second.lock()) {
                    return existing;
                }
            }
        }

        std::string path = ref.resolve();
        if (path.empty()) {
            vfLogError("Failed to resolve material instance AssetRef: {}", guid.toString());
            return nullptr;
        }

        auto result = material::MaterialInstanceAsset::load(path);
        if (!result) {
            vfLogError("Failed to load material instance: {}", path);
            return nullptr;
        }

        auto instance = std::make_shared<material::MaterialInstanceData>(std::move(*result));

        {
            std::scoped_lock lock(cacheMutex);
            materialInstanceCache[guid] = instance;
        }

        return instance;
    }

    void ResourceManager::invalidateMaterialInstanceCache(const asset::AssetRef& ref)
    {
        std::scoped_lock lock(cacheMutex);
        materialInstanceCache.erase(ref.getGUID());
    }

    std::shared_ptr<terrain::TerrainMaterialData> ResourceManager::loadTerrainMaterial(const asset::AssetRef& ref)
    {
        auto guid = ref.getGUID();
        {
            std::scoped_lock lock(cacheMutex);
            auto it = terrainMaterialCache.find(guid);
            if (it != terrainMaterialCache.end()) {
                if (auto existing = it->second.lock()) {
                    return existing;
                }
            }
        }

        std::string path = ref.resolve();
        if (path.empty()) {
            vfLogError("Failed to resolve terrain material AssetRef: {}", guid.toString());
            return nullptr;
        }

        auto result = terrain::TerrainMaterialAsset::load(path);
        if (!result) {
            vfLogError("Failed to load terrain material: {}", path);
            return nullptr;
        }

        auto terrainMat = std::make_shared<terrain::TerrainMaterialData>(std::move(*result));

        {
            std::scoped_lock lock(cacheMutex);
            terrainMaterialCache[guid] = terrainMat;
        }

        return terrainMat;
    }

    void ResourceManager::invalidateTerrainMaterialCache(const asset::AssetRef& ref)
    {
        std::scoped_lock lock(cacheMutex);
        terrainMaterialCache.erase(ref.getGUID());
    }

    std::shared_ptr<animator::AnimatorData> ResourceManager::loadAnimator(const asset::AssetRef& ref)
    {
        auto guid = ref.getGUID();
        {
            std::scoped_lock lock(cacheMutex);
            auto it = animatorCache.find(guid);
            if (it != animatorCache.end()) {
                if (auto existing = it->second.lock()) {
                    return existing;
                }
            }
        }

        std::string path = ref.resolve();
        if (path.empty()) {
            vfLogError("Failed to resolve animator AssetRef: {}", guid.toString());
            return nullptr;
        }

        auto result = animator::AnimatorAsset::load(path);
        if (!result) {
            vfLogError("Failed to load animator: {}", path);
            return nullptr;
        }

        auto animatorData = std::make_shared<animator::AnimatorData>(std::move(*result));

        {
            std::scoped_lock lock(cacheMutex);
            animatorCache[guid] = animatorData;
        }

        return animatorData;
    }
}
