#pragma once
#include "../data/AsyncLoadingTypes.hpp"
#include "resource/Types.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace services {


    struct EditorTextureData {
        void* descriptorSet = nullptr;  // ImGui descriptor set for rendering (all mips with trilinear)
        int width = 0;
        int height = 0;
        int channels = 0;
        int mipLevels = 1;
        std::vector<void*> mipDescriptorSets;  // Per-mip level descriptors for preview
        bool valid = false;
    };


    class IEditorTextureProvider {
    public:
        virtual ~IEditorTextureProvider() = default;

        // Synchronous loading (blocking) - used for small UI resources like icon atlases
        virtual EditorTextureData loadTexture(std::string_view path) = 0;
        virtual EditorTextureData loadTextureFromData(resource::TextureData&& textureData) = 0;
        virtual void releaseTexture(void* descriptorSet) = 0;

        // Async loading (non-blocking)
        virtual void loadTextureAsync(void* instanceId, std::string_view path, bool isHDR) = 0;
        virtual void cancelTextureLoading(void* instanceId) = 0;
        virtual TextureLoadingProgress getTextureLoadingProgress(void* instanceId) const = 0;
        virtual EditorTextureData getLoadedTexture(void* instanceId) = 0;
        virtual void processAsyncLoading() = 0;
    };

}
