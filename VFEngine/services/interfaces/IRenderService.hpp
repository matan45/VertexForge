#pragma once
#include "../data/DTOs.hpp"
#include <glm/glm.hpp>
#include <string>
#include <optional>

namespace services {

    // Render service interface - abstracts rendering operations
    // Presentation layer uses this instead of direct OffScreen/TextureController access
    class IRenderService {
    public:
        virtual ~IRenderService() = default;

        // ============================================
        // Viewport Rendering
        // ============================================

        // Get the current viewport texture for ImGui rendering
        virtual ViewportTextureHandle getViewportTexture() = 0;

        // Request viewport resize
        virtual void resizeViewport(uint32_t width, uint32_t height) = 0;

        // Get current viewport size
        virtual void getViewportSize(uint32_t& width, uint32_t& height) const = 0;

        // ============================================
        // IBL (Image-Based Lighting)
        // ============================================

        // Set IBL from HDR image path
        virtual bool setIBL(const std::string& hdrPath) = 0;

        // Update IBL camera matrices (called each frame with EditorCamera matrices)
        virtual void updateIBLCamera(const glm::mat4& view, const glm::mat4& projection) = 0;

        // Remove current IBL
        virtual void removeIBL() = 0;

        // Check if IBL is active
        virtual bool hasIBL() const = 0;

        // Get current IBL path (if any)
        virtual std::optional<std::string> getIBLPath() const = 0;

        // ============================================
        // Editor Textures (for UI icons, previews)
        // ============================================

        // Load a texture for editor UI (icons, previews)
        virtual EditorTextureHandle loadEditorTexture(const std::string& path) = 0;

        // Load an HDR texture for editor UI (IBL preview)
        virtual EditorTextureHandle loadEditorHDRTexture(const std::string& path) = 0;

        // Release an editor texture
        virtual void releaseEditorTexture(const EditorTextureHandle& handle) = 0;

        // ============================================
        // Render State
        // ============================================

        // Check if renderer is initialized and ready
        virtual bool isReady() const = 0;

        // Get current frame number
        virtual uint64_t getFrameNumber() const = 0;
    };

}
