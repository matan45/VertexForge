#pragma once
#include "../interfaces/IRenderService.hpp"
#include "../events/RenderEvents.hpp"
#include "../events/SceneEvents.hpp"
#include "../providers/IOffScreenProvider.hpp"
#include "../providers/IEditorTextureProvider.hpp"
#include <memory>
#include <unordered_map>

namespace services {

    class RenderServiceImpl : public IRenderService {
    public:
        /**
         * @brief Construct RenderServiceImpl with provider interfaces.
         * @param offScreenProvider Provider for viewport/offscreen rendering (required)
         * @param textureProvider Provider for editor texture loading (required)
         */
        explicit RenderServiceImpl(IOffScreenProvider* offScreenProvider,
                                   IEditorTextureProvider* textureProvider);
        ~RenderServiceImpl() override;

        // Register all command and query handlers with the EventDispatcher
        void registerEventHandlers();

        // Viewport Rendering
        ViewportTextureHandle getViewportTexture() override;
        void resizeViewport(uint32_t width, uint32_t height) override;
        void getViewportSize(uint32_t& width, uint32_t& height) const override;

        // IBL (Image-Based Lighting)
        bool setIBL(const std::string& hdrPath) override;
        void updateIBLCamera(const glm::mat4& view, const glm::mat4& projection) override;
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

        // Mesh Operations
        std::string loadMesh(const std::string& meshPath);
        void unloadMesh(const std::string& meshId);
        void updateMeshCamera(const glm::mat4& view, const glm::mat4& projection,
                              const glm::vec3& cameraPos, float time = 0.0f);
        bool isMeshLoaded(const std::string& meshPath) const;
        std::vector<std::string> getLoadedMeshes() const;
        void prepareFrameMeshes();

    private:
        IOffScreenProvider* offScreenProvider;
        IEditorTextureProvider* textureProvider;
        std::optional<std::string> currentIBLPath;
        uint32_t viewportWidth = 0;
        uint32_t viewportHeight = 0;
        uint64_t frameCounter = 0;

        // Track loaded editor textures by descriptor set for cleanup
        std::unordered_map<void*, EditorTextureHandle> loadedTextures;

        // Subscription token for mesh preloading
        events::SubscriptionToken meshDataChangedToken;
    };

}
