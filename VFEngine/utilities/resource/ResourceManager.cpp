#include "ResourceManager.hpp"
#include "TextureResource.hpp"
#include "AudioResource.hpp"
#include "FontResource.hpp"
#include "AnimationResource.hpp"
#include "MeshStreamHandle.hpp"
#include "../material/MaterialAsset.hpp"
#include "../material/MaterialInstanceAsset.hpp"
#include "../animator/AnimatorAsset.hpp"
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
        // Remove the entry if the resource is no longer referenced
        std::erase_if(textureCache, [](const auto& pair) { return pair.second.expired(); });
        std::erase_if(hdrCache, [](const auto& pair) { return pair.second.expired(); });
        std::erase_if(audioCache, [](const auto& pair) { return pair.second.expired(); });
        std::erase_if(meshCache, [](const auto& pair) { return pair.second.expired(); });
        std::erase_if(shaderCache, [](const auto& pair) { return pair.second.expired(); });
        std::erase_if(materialCache, [](const auto& pair) { return pair.second.expired(); });
        std::erase_if(materialInstanceCache, [](const auto& pair) { return pair.second.expired(); });
        std::erase_if(fontCache, [](const auto& pair) { return pair.second.expired(); });
        std::erase_if(animationCache, [](const auto& pair) { return pair.second.expired(); });
        std::erase_if(animatorCache, [](const auto& pair) { return pair.second.expired(); });
    }

    static FileType getExpectedTypeFromExtension(const std::string& ext)
    {
        if (ext == ".vfimage") return FileType::TEXTURE;
        if (ext == ".vfmesh") return FileType::MESH;
        if (ext == ".vfhdr") return FileType::HDR;
        if (ext == ".vfaudio") return FileType::AUDIO;
        if (ext == ".vfanim") return FileType::ANIMATION;
        if (ext == ".vffont") return FileType::FONT;
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
            case FileType::ANIMATOR: return "ANIMATOR";
            default: return "UNKNOWN";
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

        if (typeByte >= static_cast<uint8_t>(FileType::UNKNOWN))
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
            // TODO: Fix corrupted files - the FileType enum order changed, causing old files to have wrong headers.
            // Old files need to be re-imported or a migration tool should be created.
            // For now, trust the extension over the header.
            return expectedType;
        }

        return headerType;
    }

    std::future<std::shared_ptr<TextureData>> ResourceManager::loadTextureAsync(std::string_view path)
    {
        return loadResourceAsync<TextureData>(
            path,
            textureCache,
            [](std::string_view p) { return TextureResource::loadTexture(p); });
    }

    std::future<std::shared_ptr<HDRData>> ResourceManager::loadHDRAsync(std::string_view path)
    {
        return loadResourceAsync<HDRData>(
            path,
            hdrCache,
            [](std::string_view p) { return TextureResource::loadHDR(p); });
    }

    std::future<std::shared_ptr<AudioData>> ResourceManager::loadAudioAsync(std::string_view path)
    {
        return loadResourceAsync<AudioData>(
            path,
            audioCache,
            [](std::string_view p) { return AudioResource::loadAudio(p); });
    }

    std::future<std::shared_ptr<MeshesData>> ResourceManager::loadMeshAsync(std::string_view path)
    {
        return loadResourceAsync<MeshesData>(
            path,
            meshCache,
            [](std::string_view p) {
                return MeshStreamResource::loadAll(p);
            });
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
            [](std::string_view p) { return FontResource::loadFont(p); });
    }

    std::future<std::shared_ptr<AnimationData>> ResourceManager::loadAnimationAsync(std::string_view path)
    {
        return loadResourceAsync<AnimationData>(
            path,
            animationCache,
            [](std::string_view p) { return AnimationResource::loadAnimation(p); });
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

        vfLogInfo("All resource caches cleared");
    }

    std::shared_ptr<material::MaterialData> ResourceManager::loadMaterial(std::string_view path)
    {
        {
            std::scoped_lock lock(cacheMutex);
            auto it = materialCache.find(std::string(path));
            if (it != materialCache.end()) {
                if (auto existing = it->second.lock()) {
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

        return instance;
    }

    void ResourceManager::invalidateMaterialInstanceCache(std::string_view path)
    {
        std::scoped_lock lock(cacheMutex);
        materialInstanceCache.erase(std::string(path));
    }

    std::shared_ptr<animator::AnimatorData> ResourceManager::loadAnimator(std::string_view path)
    {
        {
            std::scoped_lock lock(cacheMutex);
            auto it = animatorCache.find(std::string(path));
            if (it != animatorCache.end()) {
                if (auto existing = it->second.lock()) {
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

        return animatorData;
    }
}
