#include "EditorTextureAdapter.hpp"
#include "../controllers/EditorTextureController.hpp"
#include "../controllers/texture/EditorTexture.hpp"

namespace core {

    EditorTextureAdapter::~EditorTextureAdapter() {
        loadedTextures.clear();
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
        result.valid = true;

        // Track for cleanup
        loadedTextures[result.descriptorSet] = std::move(texture);

        return result;
    }

    void EditorTextureAdapter::releaseTexture(void* descriptorSet) {
        loadedTextures.erase(descriptorSet);
    }

}
