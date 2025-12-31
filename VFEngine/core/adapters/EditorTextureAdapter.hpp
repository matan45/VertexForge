#pragma once
#include "../../services/providers/IEditorTextureProvider.hpp"
#include "../../services/data/AsyncLoadingTypes.hpp"
#include "../controllers/texture/EditorTexture.hpp"
#include <unordered_map>
#include <memory>

namespace loaders
{
    class AsyncTextureLoader;
}

namespace core
{
    class EditorTextureAdapter : public services::IEditorTextureProvider
    {
    private:
        // Track loaded textures for cleanup
        std::unordered_map<void*, std::unique_ptr<dto::EditorTexture>> loadedTextures;

        // Async loading
        std::unique_ptr<loaders::AsyncTextureLoader> asyncLoader;

    public:
        explicit EditorTextureAdapter();
        ~EditorTextureAdapter() override;

        services::EditorTextureData loadTexture(std::string_view path) override;
        services::EditorTextureData loadHdrTexture(std::string_view path) override;
        void releaseTexture(void* descriptorSet) override;

        // Async loading interface
        void loadTextureAsync(void* instanceId, std::string_view path, bool isHDR);
        void cancelTextureLoading(void* instanceId);
        services::TextureLoadingProgress getTextureLoadingProgress(void* instanceId) const;
        services::EditorTextureData getLoadedTexture(void* instanceId);
        void processAsyncLoading();
    };
}
