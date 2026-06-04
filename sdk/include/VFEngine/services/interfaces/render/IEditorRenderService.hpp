#pragma once
#include "IRenderService.hpp"

namespace services {

    // Editor-specific render service interface - adds editor texture functionality
    class IEditorRenderService : public IRenderService {
    public:
        ~IEditorRenderService() override = default;

        // ============================================
        // Editor Textures (for UI icons)
        // ============================================

        // Load a texture for editor UI (icons) - sync, for small resources
        virtual EditorTextureHandle loadEditorTexture(const std::string& path) = 0;

        // Release an editor texture
        virtual void releaseEditorTexture(const EditorTextureHandle& handle) = 0;
    };

}
