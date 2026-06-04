#pragma once
#include "../../interfaces/render/IEditorRenderService.hpp"
#include "../../events/render/RenderEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../providers/render/IOffScreenProvider.hpp"
#include "../../providers/render/IEditorTextureProvider.hpp"
#include "../../providers/render/IPostProcessProvider.hpp"
#include "../../data/EntityHandle.hpp"
#include <functional>
#include <unordered_map>

namespace events
{
    class EventDispatcher;
}

namespace services
{
    class EditorRenderServiceImpl : public IEditorRenderService
    {
    private:
        IOffScreenProvider* offScreenProvider;
        IEditorTextureProvider* textureProvider;
        IPostProcessProvider* postProcessProvider;
        std::optional<std::string> currentIBLPath;
        uint32_t viewportWidth = 0;
        uint32_t viewportHeight = 0;
        uint64_t frameCounter = 0;
        bool showNavmeshDebug = false;
        bool showOverdraw = false;
        uint32_t savedViewModeBeforeOverdraw = 0;
        types::ShadowDebugMode shadowDebugMode = types::ShadowDebugMode::None;
        ViewportTextureHandle lastViewportHandle{};
        bool viewportPrepared = false;  // true after prepare, consumed by render thread

        std::unordered_map<void*, EditorTextureHandle> loadedTextures;
        events::SubscriptionToken meshDataChangedToken;
        events::SubscriptionToken editorModeChangedToken;
        events::SubscriptionToken navmeshBakeCompleteToken;
        EntityHandle autoCreatedSunEntity;  // tracks auto-created Sun for cleanup

    public:
        explicit EditorRenderServiceImpl(IOffScreenProvider* offScreenProvider,
                                         IEditorTextureProvider* textureProvider,
                                         IPostProcessProvider* postProcessProvider);
        ~EditorRenderServiceImpl() override;

        void registerEventHandlers() override;

        ViewportTextureHandle getViewportTexture() override;
        void resizeViewport(uint32_t width, uint32_t height) override;

        // Called by render thread to execute the deferred GPU render
        void renderViewportDeferred();
        void renderViewportDeferred(const std::function<void()>& preRenderCallback);
        void getViewportSize(uint32_t& width, uint32_t& height) const override;

        bool setIBL(const std::string& hdrPath) override;
        void updateIBLCamera(const glm::mat4& view, const glm::mat4& projection) override;
        void removeIBL() override;
        bool hasIBL() const override;
        std::optional<std::string> getIBLPath() const override;

        EditorTextureHandle loadEditorTexture(const std::string& path) override;
        void releaseEditorTexture(const EditorTextureHandle& handle) override;

        bool isReady() const override;
        uint64_t getFrameNumber() const override;

    private:
        EditorTextureHandle loadEditorTextureFromData(resource::TextureData&& textureData);

        std::string loadMesh(const std::string& meshPath);
        void updateMeshCamera(const glm::mat4& view, const glm::mat4& projection,
                              const glm::vec3& cameraPos, float time = 0.0f);
        bool isMeshLoaded(const std::string& meshPath) const;
        std::optional<MeshBoundingBox> getMeshBoundingBox(const std::string& meshPath) const;

        void registerIBLHandlers(events::EventDispatcher& dispatcher);
        void registerTextureHandlers(events::EventDispatcher& dispatcher);
        void registerViewportHandlers(events::EventDispatcher& dispatcher);
        void registerCullingHandlers(events::EventDispatcher& dispatcher);
        void registerTerrainRenderHandlers(events::EventDispatcher& dispatcher);
        void registerPostProcessHandlers(events::EventDispatcher& dispatcher);
        void registerAtmosphereHandlers(events::EventDispatcher& dispatcher);
        void registerCloudHandlers(events::EventDispatcher& dispatcher);
        void registerWeatherHandlers(events::EventDispatcher& dispatcher);
    };
}
