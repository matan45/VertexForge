#pragma once
#include "../interfaces/IEditorRenderService.hpp"
#include "../events/RenderEvents.hpp"
#include "../events/SceneEvents.hpp"
#include "../providers/IOffScreenProvider.hpp"
#include "../providers/IEditorTextureProvider.hpp"
#include "../providers/IPostProcessProvider.hpp"
#include <unordered_map>

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

        std::unordered_map<void*, EditorTextureHandle> loadedTextures;
        events::SubscriptionToken meshDataChangedToken;
        events::SubscriptionToken editorModeChangedToken;

    public:
        explicit EditorRenderServiceImpl(IOffScreenProvider* offScreenProvider,
                                         IEditorTextureProvider* textureProvider,
                                         IPostProcessProvider* postProcessProvider);
        ~EditorRenderServiceImpl() override;

        void registerEventHandlers() override;

        ViewportTextureHandle getViewportTexture() override;
        void resizeViewport(uint32_t width, uint32_t height) override;
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
        
        void prepareCameras();
        void prepareFrameMeshes();
        void prepareFrameBillboards();
        void prepareFrameText();
        void prepareFrameCameraFrustums();
        void prepareFrameAudioSpheres();
        void prepareFrameLightGizmos();
        void prepareFramePhysicsColliders();
        void prepareFrameClusterDebug();
        void prepareFrameShadowDebug();
        void prepareFrameUICanvasOutlines();
        void prepareGrid();
    };
}
