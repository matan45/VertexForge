#pragma once
#include "IRenderService.hpp"

namespace services {

    // Editor-specific render service interface - adds editor texture functionality
    class IEditorRenderService : public IRenderService {
    public:
        ~IEditorRenderService() override = default;

        // ============================================
        // Editor Textures (for UI icons, previews)
        // ============================================

        // Load a texture for editor UI (icons, previews)
        virtual EditorTextureHandle loadEditorTexture(const std::string& path) = 0;

        // Load an HDR texture for editor UI (IBL preview)
        virtual EditorTextureHandle loadEditorHDRTexture(const std::string& path) = 0;

        // Release an editor texture
        virtual void releaseEditorTexture(const EditorTextureHandle& handle) = 0;
    };

}
