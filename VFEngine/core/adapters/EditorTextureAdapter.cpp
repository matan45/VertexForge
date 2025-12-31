#include "EditorTextureAdapter.hpp"
#include "../controllers/EditorTextureController.hpp"
#include "../controllers/texture/EditorTexture.hpp"
#include "../../graphics/loaders/AsyncTextureLoader.hpp"
#include "print/Logger.hpp"

namespace core {

    EditorTextureAdapter::EditorTextureAdapter()
        : asyncLoader(std::make_unique<loaders::AsyncTextureLoader>())
    {
    }

    EditorTextureAdapter::~EditorTextureAdapter() {
        loadedTextures.clear();
        asyncLoader.reset();
    }

    services::EditorTextureData EditorTextureAdapter::loadTexture(std::string_view path) {
        auto texture = controllers::EditorTextureController::loadTexture(path);

        services::EditorTextureData result;
        if (!texture) {
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

    services::EditorTextureData EditorTextureAdapter::loadHdrTexture(std::string_view path) {
        auto texture = controllers::EditorTextureController::loadHdrTexture(path);

        services::EditorTextureData result;
        if (!texture) {
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

    void EditorTextureAdapter::releaseTexture(void* descriptorSet) {
        if (descriptorSet == nullptr) {
            loggerWarning("EditorTextureAdapter::releaseTexture called with null descriptor");
            return;
        }

        auto it = loadedTextures.find(descriptorSet);
        if (it == loadedTextures.end()) {
            loggerWarning("EditorTextureAdapter::releaseTexture called with unknown descriptor {:p}", descriptorSet);
            return;
        }

        loadedTextures.erase(it);
    }

    void EditorTextureAdapter::loadTextureAsync(void* instanceId, std::string_view path, bool isHDR) {
        if (asyncLoader) {
            asyncLoader->startLoad(instanceId, std::string(path), isHDR);
        }
    }

    void EditorTextureAdapter::cancelTextureLoading(void* instanceId) {
        if (asyncLoader) {
            asyncLoader->cancelLoad(instanceId);
        }
    }

    services::TextureLoadingProgress EditorTextureAdapter::getTextureLoadingProgress(void* instanceId) const {
        if (asyncLoader) {
            return asyncLoader->getProgress(instanceId);
        }
        return services::TextureLoadingProgress{};
    }

    services::EditorTextureData EditorTextureAdapter::getLoadedTexture(void* instanceId) {
        services::EditorTextureData result;

        if (!asyncLoader || !asyncLoader->isLoadComplete(instanceId)) {
            return result;
        }

        auto texture = asyncLoader->takeTexture(instanceId);
        if (!texture) {
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

        // Clean up from async loader
        asyncLoader->clearCompleted();

        return result;
    }

    void EditorTextureAdapter::processAsyncLoading() {
        if (!asyncLoader) {
            return;
        }

        // Check for pending CPU -> GPU transfers
        if (asyncLoader->update()) {
            void* readyInstance = asyncLoader->getReadyForGPUUpload();
            if (readyInstance) {
                asyncLoader->processGPUUpload(readyInstance);
            }
        }
    }

}
