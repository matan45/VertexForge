#include "EditorTextureAdapter.hpp"
#include "../../controllers/EditorTextureController.hpp"
#include "../../controllers/texture/EditorTexture.hpp"
#include "../../graphics/loaders/AsyncTextureLoader.hpp"
#include "print/Log.hpp"

namespace core
{
    EditorTextureAdapter::EditorTextureAdapter()
        : asyncLoader(std::make_unique<loaders::AsyncTextureLoader>())
    {
    }

    EditorTextureAdapter::~EditorTextureAdapter()
    {
        // Cancel pending async loads first, then clear textures
        asyncLoader.reset();
        loadedTextures.clear();
    }

    services::EditorTextureData EditorTextureAdapter::loadTexture(std::string_view path)
    {
        auto texture = controllers::EditorTextureController::loadTexture(path);

        services::EditorTextureData result;
        if (!texture)
        {
            return result;
        }

        result.descriptorSet = texture->getDescriptorSet();
        result.width = texture->getWidth();
        result.height = texture->getHeight();
        result.channels = texture->getNumbersOfChannels();
        result.mipLevels = texture->getMipLevels();
        result.mipDescriptorSets = texture->getMipDescriptorSets();
        result.valid = true;

        // Track for cleanup
        loadedTextures[result.descriptorSet] = std::move(texture);

        return result;
    }

    services::EditorTextureData EditorTextureAdapter::loadTextureFromData(resource::TextureData&& textureData)
    {
        auto texture = controllers::EditorTextureController::loadTextureFromData(textureData);

        services::EditorTextureData result;
        if (!texture)
        {
            return result;
        }

        result.descriptorSet = texture->getDescriptorSet();
        result.width = texture->getWidth();
        result.height = texture->getHeight();
        result.channels = texture->getNumbersOfChannels();
        result.mipLevels = texture->getMipLevels();
        result.mipDescriptorSets = texture->getMipDescriptorSets();
        result.valid = true;

        // Track for cleanup
        loadedTextures[result.descriptorSet] = std::move(texture);

        return result;
    }

    void EditorTextureAdapter::releaseTexture(void* descriptorSet)
    {
        if (descriptorSet == nullptr)
        {
            vfLogWarning("EditorTextureAdapter::releaseTexture called with null descriptor");
            return;
        }

        auto it = loadedTextures.find(descriptorSet);
        if (it == loadedTextures.end())
        {
            vfLogWarning("EditorTextureAdapter::releaseTexture called with unknown descriptor {:p}", descriptorSet);
            return;
        }

        loadedTextures.erase(it);
    }

    void EditorTextureAdapter::loadTextureAsync(void* instanceId, std::string_view path, bool isHDR)
    {
        if (asyncLoader)
        {
            asyncLoader->startLoad(instanceId, std::string(path), isHDR);
        }
    }

    void EditorTextureAdapter::cancelTextureLoading(void* instanceId)
    {
        if (asyncLoader)
        {
            asyncLoader->cancelLoad(instanceId);
        }
    }

    services::TextureLoadingProgress EditorTextureAdapter::getTextureLoadingProgress(void* instanceId) const
    {
        if (asyncLoader)
        {
            return asyncLoader->getProgress(instanceId);
        }
        return services::TextureLoadingProgress{};
    }

    services::EditorTextureData EditorTextureAdapter::getLoadedTexture(void* instanceId)
    {
        services::EditorTextureData result;

        if (!asyncLoader || !asyncLoader->isLoadComplete(instanceId))
        {
            return result;
        }

        auto texture = asyncLoader->takeTexture(instanceId);
        if (!texture)
        {
            return result;
        }

        result.descriptorSet = texture->getDescriptorSet();
        result.width = texture->getWidth();
        result.height = texture->getHeight();
        result.channels = texture->getNumbersOfChannels();
        result.mipLevels = texture->getMipLevels();
        result.mipDescriptorSets = texture->getMipDescriptorSets();
        result.valid = true;

        // Track for cleanup
        loadedTextures[result.descriptorSet] = std::move(texture);

        return result;
    }

    void EditorTextureAdapter::processAsyncLoading()
    {
        if (!asyncLoader)
        {
            return;
        }

        // Limit GPU uploads per frame to prevent frame spikes
        // Multiple textures finishing simultaneously will be spread across frames
        constexpr int MAX_UPLOADS_PER_FRAME = 3;
        int uploadsThisFrame = 0;

        // Process pending CPU -> GPU transfers this frame
        while (asyncLoader->update() && uploadsThisFrame < MAX_UPLOADS_PER_FRAME)
        {
            void* readyInstance = asyncLoader->getReadyForGPUUpload();
            if (!readyInstance)
            {
                break;
            }
            if (asyncLoader->processGPUUpload(readyInstance))
            {
                uploadsThisFrame++;
            }
            else
            {
                vfLogWarning("EditorTextureAdapter: GPU upload failed for instance {:p}", readyInstance);
            }
        }

        // Clean up failed/cancelled loads to prevent memory leaks
        asyncLoader->clearFinishedLoads();
    }
}
