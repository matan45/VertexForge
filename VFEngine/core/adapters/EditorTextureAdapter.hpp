#pragma once
#include "../../services/providers/IEditorTextureProvider.hpp"
#include "../controllers/texture/EditorTexture.hpp"
#include <unordered_map>
#include <memory>

namespace core {

    /**
     * @brief Adapter that implements IEditorTextureProvider by wrapping EditorTextureController.
     *
     * This class bridges the Services layer with the Core layer's texture loading functionality,
     * allowing Services to load editor textures without direct dependencies on Core.
     */
    class EditorTextureAdapter : public services::IEditorTextureProvider {
    public:
        EditorTextureAdapter() = default;
        ~EditorTextureAdapter() override;

        // IEditorTextureProvider implementation
        services::EditorTextureData loadTexture(std::string_view path) override;
        services::EditorTextureData loadHdrTexture(std::string_view path) override;
        void releaseTexture(void* descriptorSet) override;

    private:
        // Track loaded textures for cleanup
        std::unordered_map<void*, std::unique_ptr<dto::EditorTexture>> loadedTextures;
    };

}
