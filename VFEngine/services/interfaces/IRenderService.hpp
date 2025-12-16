#pragma once
#include "../data/DTOs.hpp"
#include <glm/glm.hpp>
#include <string>
#include <optional>

namespace services {

    // Base render service interface - shared between Editor and Runtime
    class IRenderService {
    public:
        virtual ~IRenderService() = default;

        // Register CQRS event handlers
        virtual void registerEventHandlers() = 0;

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
        // Render State
        // ============================================

        // Check if renderer is initialized and ready
        virtual bool isReady() const = 0;

        // Get current frame number
        virtual uint64_t getFrameNumber() const = 0;
    };

}
