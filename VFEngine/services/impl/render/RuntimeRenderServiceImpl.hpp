#pragma once
#include "../../interfaces/render/IRenderService.hpp"
#include "../../events/render/RenderEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../providers/render/IOffScreenProvider.hpp"
#include "../../providers/render/IPostProcessProvider.hpp"

namespace services {

    class RuntimeRenderServiceImpl : public IRenderService {
    private:
        IOffScreenProvider* offScreenProvider;
        IPostProcessProvider* postProcessProvider;
        std::optional<std::string> currentIBLPath;
        uint32_t viewportWidth = 0;
        uint32_t viewportHeight = 0;
        uint64_t frameCounter = 0;

        events::SubscriptionToken meshDataChangedToken;
    public:
        explicit RuntimeRenderServiceImpl(IOffScreenProvider* offScreenProvider,
                                         IPostProcessProvider* postProcessProvider);
        ~RuntimeRenderServiceImpl() override = default;

        void registerEventHandlers() override;
        
        ViewportTextureHandle getViewportTexture() override;
        void resizeViewport(uint32_t width, uint32_t height) override;
        void getViewportSize(uint32_t& width, uint32_t& height) const override;
        
        bool setIBL(const std::string& hdrPath) override;
        void updateIBLCamera(const glm::mat4& view, const glm::mat4& projection) override;
        void removeIBL() override;
        bool hasIBL() const override;
        std::optional<std::string> getIBLPath() const override;

        bool isReady() const override;
        uint64_t getFrameNumber() const override;

    private:
        std::string loadMesh(const std::string& meshPath);
        void updateMeshCamera(const glm::mat4& view, const glm::mat4& projection,
                              const glm::vec3& cameraPos, float time = 0.0f);
        bool isMeshLoaded(const std::string& meshPath) const;
        void prepareCameras();
        std::optional<MeshBoundingBox> getMeshBoundingBox(const std::string& meshPath) const;
        void prepareFrameMeshes();
        void prepareFrameText();
        void prepareFrameUIImages();
    };

}
