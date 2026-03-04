#pragma once
#include "../../services/providers/render/IEditorTextureProvider.hpp"
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
        std::unordered_map<void*, std::unique_ptr<dto::EditorTexture>> loadedTextures;

        std::unique_ptr<loaders::AsyncTextureLoader> asyncLoader;

    public:
        explicit EditorTextureAdapter();
        ~EditorTextureAdapter() override;

        services::EditorTextureData loadTexture(std::string_view path) override;
        services::EditorTextureData loadTextureFromData(resource::TextureData&& textureData) override;
        void releaseTexture(void* descriptorSet) override;

        // Async loading interface
        void loadTextureAsync(void* instanceId, std::string_view path, bool isHDR) override;
        void cancelTextureLoading(void* instanceId) override;
        services::TextureLoadingProgress getTextureLoadingProgress(void* instanceId) const override;
        services::EditorTextureData getLoadedTexture(void* instanceId) override;
        void processAsyncLoading() override;
    };
}
