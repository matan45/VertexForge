#pragma once
#include "../data/DTOs.hpp"
#include <glm/glm.hpp>
#include <string>
#include <optional>

namespace services {
    
    class IRenderService {
    public:
        virtual ~IRenderService() = default;
        
        virtual void registerEventHandlers() = 0;

        // ============================================
        // Viewport Rendering
        // ============================================
        
        virtual ViewportTextureHandle getViewportTexture() = 0;
        
        virtual void resizeViewport(uint32_t width, uint32_t height) = 0;
        
        virtual void getViewportSize(uint32_t& width, uint32_t& height) const = 0;

        // ============================================
        // IBL (Image-Based Lighting)
        // ============================================
        
        virtual bool setIBL(const std::string& hdrPath) = 0;
        
        virtual void updateIBLCamera(const glm::mat4& view, const glm::mat4& projection) = 0;
        
        virtual void removeIBL() = 0;
        
        virtual bool hasIBL() const = 0;
        
        virtual std::optional<std::string> getIBLPath() const = 0;

        // ============================================
        // Render State
        // ============================================
        
        virtual bool isReady() const = 0;
        
        virtual uint64_t getFrameNumber() const = 0;
    };

}
