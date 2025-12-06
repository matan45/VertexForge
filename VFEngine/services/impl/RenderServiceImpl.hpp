#pragma once
#include "../interfaces/IRenderService.hpp"
#include "../events/RenderEvents.hpp"
#include <memory>
#include <unordered_map>

// Forward declarations
namespace controllers {
    class OffScreen;
}

namespace dto {
    class EditorTexture;
}

namespace services {

    class RenderServiceImpl : public IRenderService {
    public:
        explicit RenderServiceImpl(controllers::OffScreen* offScreen);
        ~RenderServiceImpl() override;

        // Register all command and query handlers with the EventDispatcher
        void registerEventHandlers();

        // Viewport Rendering
        ViewportTextureHandle getViewportTexture() override;
        void resizeViewport(uint32_t width, uint32_t height) override;
        void getViewportSize(uint32_t& width, uint32_t& height) const override;

        // IBL (Image-Based Lighting)
        bool setIBL(const std::string& hdrPath) override;
        void removeIBL() override;
        bool hasIBL() const override;
        std::optional<std::string> getIBLPath() const override;

        // Editor Textures
        EditorTextureHandle loadEditorTexture(const std::string& path) override;
        EditorTextureHandle loadEditorHDRTexture(const std::string& path) override;
        void releaseEditorTexture(const EditorTextureHandle& handle) override;

        // Render State
        bool isReady() const override;
        uint64_t getFrameNumber() const override;

    private:
        controllers::OffScreen* offScreen;
        std::optional<std::string> currentIBLPath;
        uint32_t viewportWidth = 0;
        uint32_t viewportHeight = 0;
        uint64_t frameCounter = 0;

        // Track loaded editor textures for cleanup
        std::unordered_map<void*, std::unique_ptr<dto::EditorTexture>> loadedTextures;
    };

}
