#pragma once
#include "../../services/providers/IEditorTextureProvider.hpp"
#include "../controllers/texture/EditorTexture.hpp"
#include <unordered_map>
#include <memory>

namespace core
{
    class EditorTextureAdapter : public services::IEditorTextureProvider
    {
    private:
        // Track loaded textures for cleanup
        std::unordered_map<void*, std::unique_ptr<dto::EditorTexture>> loadedTextures;

    public:
        explicit EditorTextureAdapter() = default;
        ~EditorTextureAdapter() override;
        
        services::EditorTextureData loadTexture(std::string_view path) override;
        services::EditorTextureData loadHdrTexture(std::string_view path) override;
        void releaseTexture(void* descriptorSet) override;
    };
}
