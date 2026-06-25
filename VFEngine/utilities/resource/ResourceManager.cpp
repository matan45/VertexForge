#include "ResourceManager.hpp"
#include "ResourceLoadScheduler.hpp"
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
#include "../retargeting/RetargetAsset.hpp"
#include "VFSHelpers.hpp"
#include <algorithm>
#include <cctype>

namespace resource
{
    void ResourceManager::periodicCleanup()
    {
        using namespace std::chrono_literals;
        std::unique_lock lock(cacheMutex);
        while (running)
        {
            cleanupCondition.wait_for(lock, 1min);
            if (!running) break;
            unloadUnusedResources();
        }
    }

    void ResourceManager::unloadUnusedResources()
    {
        auto& lifecycle = AssetLifecycleManager::instance();

        auto releaseExpired = [&lifecycle](auto& cache) {
            std::erase_if(cache, [&lifecycle](const auto& pair) {
                if (pair.second.expired()) { lifecycle.release(pair.first); return true; }
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

        auto cleanExpired = [](auto& cache) {
            std::erase_if(cache, [](const auto& pair) { return pair.second.expired(); });
        };
        cleanExpired(materialCache);
        cleanExpired(materialInstanceCache);
        cleanExpired(terrainMaterialCache);
        cleanExpired(humanoidRigCache);
        cleanExpired(retargetMapCache);
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

    static FileType readBinaryHeader(const fs::path& filePath)
    {
        auto data = resource::readFileBytes(filePath.string());
        if (data.empty())
        {
            vfLogError("Failed to read file: {}", filePath.string());
            return FileType::UNKNOWN;
        }

        uint8_t typeByte = data[0];

        if (!isValidFileType(typeByte))
        {
            vfLogError("Invalid file type header {} in file: {}", typeByte, filePath.string());
            return FileType::UNKNOWN;
        }
        return static_cast<FileType>(typeByte);
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

        if (extension == ".vfscene")
            return FileType::SCENE;

        FileType headerType = readBinaryHeader(filePath);
        if (headerType == FileType::UNKNOWN)
            return FileType::UNKNOWN;

        FileType expectedType = getExpectedTypeFromExtension(extension);
        if (expectedType != FileType::UNKNOWN && headerType != expectedType)
        {
            vfLogWarning("File header/extension mismatch: {} has header {} but extension expects {} - using extension type",
                       filePath.string(), getFileTypeName(headerType), getFileTypeName(expectedType));
            return expectedType;
        }
        return headerType;
    }

    std::future<std::shared_ptr<TextureData>> ResourceManager::loadTextureAsync(const asset::AssetRef& ref, const LoadHint& hint, CancellationToken::Ptr cancellation)
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
            },
            hint, std::move(cancellation));
    }

    std::future<std::shared_ptr<HDRData>> ResourceManager::loadHDRAsync(const asset::AssetRef& ref, const LoadHint& hint, CancellationToken::Ptr cancellation)
    {
        return loadResourceAsync<HDRData>(
            ref,
            hdrCache,
            pendingHDRLoads,
            [](const std::string& p) { return TextureResource::loadHDR(p); },
            AssetType::HDR,
            [](const HDRData& hdr) -> size_t {
                return hdr.getDataSize();
            },
            hint, std::move(cancellation));
    }

    std::future<std::shared_ptr<AudioData>> ResourceManager::loadAudioAsync(const asset::AssetRef& ref, const LoadHint& hint, CancellationToken::Ptr cancellation)
    {
        return loadResourceAsync<AudioData>(
            ref,
            audioCache,
            pendingAudioLoads,
            [](const std::string& p) { return AudioResource::loadAudio(p); },
            AssetType::Audio,
            [](const AudioData& audio) -> size_t {
                return audio.data.size() * sizeof(short);
            },
            hint, std::move(cancellation));
    }

    std::future<std::shared_ptr<MeshesData>> ResourceManager::loadMeshAsync(const asset::AssetRef& ref, const LoadHint& hint, CancellationToken::Ptr cancellation)
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
            },
            hint, std::move(cancellation));
    }

    std::future<std::shared_ptr<std::vector<ShaderModel>>> ResourceManager::loadShaderAsync(std::string_view path)
    {
        std::string key(path);
        if (key.empty()) {
            vfLogError("Empty path provided for shader loading");
            return make_ready_future(std::shared_ptr<std::vector<ShaderModel>>(nullptr));
        }

        {
            std::scoped_lock lock(cacheMutex);
            auto cacheIt = shaderCache.find(key);
            if (cacheIt != shaderCache.end()) {
                if (auto resource = cacheIt->second.lock())
                    return make_ready_future(resource);
            }
            auto pendingIt = pendingShaderLoads.find(key);
            if (pendingIt != pendingShaderLoads.end()) {
                return std::async(std::launch::deferred, [sf = pendingIt->second]() mutable { return sf.get(); });
            }
        }

        // Routed through the scheduler like asset loads (priority handling,
        // dedup, profiler visibility). Critical importance dispatches
        // immediately past the concurrency cap — pipeline creation blocks on
        // these futures.
        auto sharedPromise = std::make_shared<std::promise<std::shared_ptr<std::vector<ShaderModel>>>>();
        auto resultFuture = sharedPromise->get_future();
        auto sharedResultPromise = std::make_shared<std::promise<std::shared_ptr<std::vector<ShaderModel>>>>();
        std::shared_future<std::shared_ptr<std::vector<ShaderModel>>> sharedFuture =
            sharedResultPromise->get_future().share();
        {
            std::scoped_lock lock(cacheMutex);
            pendingShaderLoads[key] = sharedFuture;
        }

        pendingAsyncOps.fetch_add(1, std::memory_order_relaxed);

        LoadRequest request;
        request.hint.importance = LoadImportance::Critical;
        request.debugName = key;
        request.progress = LoadProgress::create();
        request.computedPriority = ResourceLoadScheduler::computePriority(request.hint, {0.0f, 0.0f, 0.0f});
        request.executeLoad = [key, sharedPromise, sharedResultPromise,
            progress = request.progress]() mutable {
            struct AsyncGuard { ~AsyncGuard() { pendingAsyncOps.fetch_sub(1, std::memory_order_release); } } guard;
            try {
                if (shuttingDown.load(std::memory_order_acquire)) {
                    progress->setStage(LoadStage::Cancelled);
                    std::scoped_lock lock(cacheMutex);
                    pendingShaderLoads.erase(key);
                    sharedResultPromise->set_value(nullptr);
                    sharedPromise->set_value(nullptr);
                    return;
                }

                auto resource = std::make_shared<std::vector<ShaderModel>>(ShaderResource::readShaderFile(key));

                {
                    std::scoped_lock lock(cacheMutex);
                    pendingShaderLoads.erase(key);
                    if (resource && !shuttingDown.load(std::memory_order_acquire))
                        shaderCache[key] = resource;
                }

                sharedResultPromise->set_value(resource);
                sharedPromise->set_value(resource);
            }
            catch (const std::exception& e) {
                progress->setStage(LoadStage::Failed);
                std::scoped_lock lock(cacheMutex);
                pendingShaderLoads.erase(key);
                vfLogError("Exception loading shader '{}': {}", key, e.what());
                sharedResultPromise->set_value(nullptr);
                sharedPromise->set_value(nullptr);
            }
            catch (...) {
                progress->setStage(LoadStage::Failed);
                std::scoped_lock lock(cacheMutex);
                pendingShaderLoads.erase(key);
                vfLogError("Unknown exception loading shader: {}", key);
                sharedResultPromise->set_value(nullptr);
                sharedPromise->set_value(nullptr);
            }
        };

        ResourceLoadScheduler::instance().submit(std::move(request));

        return resultFuture;
    }

    std::future<std::shared_ptr<FontData>> ResourceManager::loadFontAsync(const asset::AssetRef& ref, const LoadHint& hint, CancellationToken::Ptr cancellation)
    {
        return loadResourceAsync<FontData>(
            ref,
            fontCache,
            pendingFontLoads,
            [](const std::string& p) { return FontResource::loadFont(p); },
            AssetType::Font,
            nullptr,
            hint, std::move(cancellation));
    }

    std::future<std::shared_ptr<AnimationData>> ResourceManager::loadAnimationAsync(const asset::AssetRef& ref, const LoadHint& hint, CancellationToken::Ptr cancellation)
    {
        return loadResourceAsync<AnimationData>(
            ref,
            animationCache,
            pendingAnimationLoads,
            [](const std::string& p) { return AnimationResource::loadAnimation(p); },
            AssetType::Animation,
            [](const AnimationData& anim) -> size_t {
                size_t total = 0;
                for (const auto& ch : anim.channels) {
                    total += ch.boneName.capacity();
                    total += ch.positionKeys.size() * sizeof(PositionKey);
                    total += ch.rotationKeys.size() * sizeof(RotationKey);
                    total += ch.scalingKeys.size() * sizeof(ScaleKey);
                }
                total += anim.events.size() * sizeof(animator::AnimationEvent);
                return total;
            },
            hint, std::move(cancellation));
    }

    void ResourceManager::init()
    {
        running = true;
        shuttingDown = false;
        pendingAsyncOps = 0;
        ResourceLoadScheduler::instance().init();
        cleanupThread = std::jthread(&ResourceManager::periodicCleanup);
    }

    void ResourceManager::cleanUp()
    {
        shuttingDown.store(true, std::memory_order_release);
        ResourceLoadScheduler::instance().shutdown();

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
        humanoidRigCache.clear();
        retargetMapCache.clear();

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

    template <typename T, typename LoadFunc>
    static std::shared_ptr<T> loadSyncCached(
        const asset::AssetRef& ref,
        std::unordered_map<asset::AssetGUID, std::weak_ptr<T>, asset::AssetGUID::Hash>& cache,
        std::mutex& mtx, LoadFunc loader, const char* typeName)
    {
        auto guid = ref.getGUID();
        {
            std::scoped_lock lock(mtx);
            auto it = cache.find(guid);
            if (it != cache.end()) {
                if (auto existing = it->second.lock()) return existing;
            }
        }

        std::string path = ref.resolve();
        if (path.empty()) {
            vfLogError("Failed to resolve {} AssetRef: {}", typeName, guid.toString());
            return nullptr;
        }

        auto result = loader(path);
        if (!result) {
            vfLogError("Failed to load {}: {}", typeName, path);
            return nullptr;
        }

        auto resource = std::make_shared<T>(std::move(*result));
        { std::scoped_lock lock(mtx); cache[guid] = resource; }
        return resource;
    }

    std::shared_ptr<material::MaterialData> ResourceManager::loadMaterial(const asset::AssetRef& ref)
    {
        if (!ref.isValid()) return nullptr;
        return loadSyncCached<material::MaterialData>(ref, materialCache, cacheMutex,
            [](const std::string& p) { return material::MaterialAsset::load(p); }, "material");
    }

    void ResourceManager::invalidateMaterialCache(const asset::AssetRef& ref)
    {
        std::scoped_lock lock(cacheMutex);
        materialCache.erase(ref.getGUID());
    }

    std::shared_ptr<material::MaterialInstanceData> ResourceManager::loadMaterialInstance(const asset::AssetRef& ref)
    {
        if (!ref.isValid()) return nullptr;
        return loadSyncCached<material::MaterialInstanceData>(ref, materialInstanceCache, cacheMutex,
            [](const std::string& p) { return material::MaterialInstanceAsset::load(p); }, "material instance");
    }

    void ResourceManager::invalidateMaterialInstanceCache(const asset::AssetRef& ref)
    {
        std::scoped_lock lock(cacheMutex);
        materialInstanceCache.erase(ref.getGUID());
    }

    std::shared_ptr<terrain::TerrainMaterialData> ResourceManager::loadTerrainMaterial(const asset::AssetRef& ref)
    {
        return loadSyncCached<terrain::TerrainMaterialData>(ref, terrainMaterialCache, cacheMutex,
            [](const std::string& p) { return terrain::TerrainMaterialAsset::load(p); }, "terrain material");
    }

    void ResourceManager::invalidateTerrainMaterialCache(const asset::AssetRef& ref)
    {
        std::scoped_lock lock(cacheMutex);
        terrainMaterialCache.erase(ref.getGUID());
    }

    std::shared_ptr<animator::AnimatorData> ResourceManager::loadAnimator(const asset::AssetRef& ref)
    {
        return loadSyncCached<animator::AnimatorData>(ref, animatorCache, cacheMutex,
            [](const std::string& p) { return animator::AnimatorAsset::load(p); }, "animator");
    }

    std::shared_ptr<retargeting::HumanoidRigData> ResourceManager::loadHumanoidRig(const asset::AssetRef& ref)
    {
        if (!ref.isValid()) return nullptr;
        return loadSyncCached<retargeting::HumanoidRigData>(ref, humanoidRigCache, cacheMutex,
            [](const std::string& p) { return retargeting::HumanoidRigAsset::load(p); }, "humanoid rig");
    }

    std::shared_ptr<retargeting::RetargetMapData> ResourceManager::loadRetargetMap(const asset::AssetRef& ref)
    {
        if (!ref.isValid()) return nullptr;
        return loadSyncCached<retargeting::RetargetMapData>(ref, retargetMapCache, cacheMutex,
            [](const std::string& p) { return retargeting::RetargetMapAsset::load(p); }, "retarget map");
    }
}
