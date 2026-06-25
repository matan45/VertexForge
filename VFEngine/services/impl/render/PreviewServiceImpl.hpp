#pragma once
#include "../../interfaces/render/IPreviewService.hpp"
#include "../../events/render/PreviewEvents.hpp"
#include "../../events/render/PrefabRigPreviewEvents.hpp"
#include "../../events/render/UILayerPreviewEvents.hpp"
#include "../../events/animation/AnimationPreviewEvents.hpp"
#include "../../events/vfx/VFXPreviewEvents.hpp"

namespace events
{
    class EventDispatcher;
}

namespace services
{
    class IMaterialPreviewProvider;
    class IMeshPreviewProvider;
    class IAnimationPreviewProvider;
    class IVFXPreviewProvider;
    class IPrefabRigPreviewProvider;
    class IUILayerPreviewProvider;


    class PreviewServiceImpl : public IPreviewService
    {
    private:
        IMaterialPreviewProvider* materialProvider;
        IMeshPreviewProvider* meshProvider;
        IAnimationPreviewProvider* animationProvider;
        IVFXPreviewProvider* vfxProvider;
        IPrefabRigPreviewProvider* prefabRigProvider;
        IUILayerPreviewProvider* uiLayerProvider;

    public:
        explicit PreviewServiceImpl(IMaterialPreviewProvider* materialProvider, IMeshPreviewProvider* meshProvider,
                                    IAnimationPreviewProvider* animationProvider = nullptr,
                                    IVFXPreviewProvider* vfxProvider = nullptr,
                                    IPrefabRigPreviewProvider* prefabRigProvider = nullptr,
                                    IUILayerPreviewProvider* uiLayerProvider = nullptr);
        ~PreviewServiceImpl() override;

        void registerEventHandlers() override;

        void initMaterialPreview(PreviewInstanceId instanceId) override;
        void cleanUpMaterialPreview(PreviewInstanceId instanceId) override;
        void setMaterialParams(PreviewInstanceId instanceId, const MaterialPreviewParams& params) override;
        void updateMaterialCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                  const glm::vec3& cameraPos, float time = 0.0f) override;
        [[nodiscard]] ViewportTextureHandle renderMaterialPreview(PreviewInstanceId instanceId) override;
        [[nodiscard]] std::string getMaterialShaderError(PreviewInstanceId instanceId) const override;

        void initMeshPreview(PreviewInstanceId instanceId) override;
        void cleanUpMeshPreview(PreviewInstanceId instanceId) override;
        [[nodiscard]] std::vector<SubMeshInfo> getPreviewMeshSubMeshInfo(PreviewInstanceId instanceId) const override;
        [[nodiscard]] std::vector<LODInfo> getPreviewMeshLODInfo(PreviewInstanceId instanceId) const override;
        [[nodiscard]] math::AABB getPreviewMeshBounds(PreviewInstanceId instanceId) const override;
        void setMeshPreviewParams(PreviewInstanceId instanceId, const MeshPreviewParams& params) override;
        void setPreviewEnvironment(PreviewInstanceId instanceId, const PreviewEnvironmentParams& params) override;
        void updateMeshCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                              const glm::vec3& cameraPos) override;
        [[nodiscard]] ViewportTextureHandle renderMeshPreview(PreviewInstanceId instanceId) override;

        void loadPreviewMeshAsync(PreviewInstanceId instanceId, const std::string& meshPath) override;
        void cancelMeshLoading(PreviewInstanceId instanceId) override;
        [[nodiscard]] MeshLoadingProgress getMeshLoadingProgress(PreviewInstanceId instanceId) const override;
        void processAsyncLoading() override;

        void initAnimationPreview(PreviewInstanceId instanceId) override;
        void cleanUpAnimationPreview(PreviewInstanceId instanceId) override;
        bool loadAnimationPreviewMesh(PreviewInstanceId instanceId, const std::string& meshPath) override;
        bool loadAnimationPreviewAnimation(PreviewInstanceId instanceId, const std::string& animPath) override;

        void playAnimation(PreviewInstanceId instanceId) override;
        void pauseAnimation(PreviewInstanceId instanceId) override;
        void stopAnimation(PreviewInstanceId instanceId) override;
        [[nodiscard]] bool isAnimationPlaying(PreviewInstanceId instanceId) const override;

        void setAnimationPlaybackTime(PreviewInstanceId instanceId, float timeSeconds) override;
        [[nodiscard]] float getAnimationPlaybackTime(PreviewInstanceId instanceId) const override;

        void setAnimationLooping(PreviewInstanceId instanceId, bool loop) override;
        void setAnimationPlaybackSpeed(PreviewInstanceId instanceId, float speed) override;

        void updateAnimationPreview(PreviewInstanceId instanceId, float deltaTime) override;
        void setAnimationPreviewParams(PreviewInstanceId instanceId, const AnimationPreviewParams& params) override;
        void setAnimationPreviewEnvironment(PreviewInstanceId instanceId,
                                            const PreviewEnvironmentParams& params) override;
        void updateAnimationCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                   const glm::mat4& projection, const glm::vec3& cameraPos) override;

        [[nodiscard]] ViewportTextureHandle renderAnimationPreview(PreviewInstanceId instanceId) override;
        [[nodiscard]] std::vector<EvaluatedBoneInfo>
        getAnimationPreviewEvaluatedBones(PreviewInstanceId instanceId) const override;

        // VFX Preview
        void initVFXPreview(PreviewInstanceId instanceId) override;
        void cleanUpVFXPreview(PreviewInstanceId instanceId) override;

        void setVFXParams(PreviewInstanceId instanceId, const VFXPreviewParams& params) override;

        void updateVFXCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                             const glm::mat4& projection, const glm::vec3& cameraPos, float time) override;
        void updateVFXSimulation(PreviewInstanceId instanceId, float deltaTime) override;

        void playVFX(PreviewInstanceId instanceId) override;
        void pauseVFX(PreviewInstanceId instanceId) override;
        void stopVFX(PreviewInstanceId instanceId) override;

        [[nodiscard]] ViewportTextureHandle renderVFXPreview(PreviewInstanceId instanceId) override;

        // Prefab Rig Preview (VK-1433)
        void initPrefabRigPreview(PreviewInstanceId instanceId) override;
        bool buildPrefabRigPreview(PreviewInstanceId instanceId, const PrefabRigDescDTO& desc) override;
        bool updatePrefabRigPreviewTransforms(PreviewInstanceId instanceId,
                                              const PrefabRigDescDTO& desc) override;
        void cleanUpPrefabRigPreview(PreviewInstanceId instanceId) override;
        [[nodiscard]] bool isPrefabRigPreviewBuilt(PreviewInstanceId instanceId) const override;
        [[nodiscard]] size_t getPrefabRigPartCount(PreviewInstanceId instanceId) const override;

        void updatePrefabRigPreview(PreviewInstanceId instanceId, float deltaTime) override;
        void updatePrefabRigCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                   const glm::mat4& projection, const glm::vec3& cameraPos) override;
        void setPrefabRigEnvironment(PreviewInstanceId instanceId, const PreviewEnvironmentParams& params) override;
        void setPrefabRigRootMatrix(PreviewInstanceId instanceId, const glm::mat4& model) override;

        [[nodiscard]] ViewportTextureHandle renderPrefabRigPreview(PreviewInstanceId instanceId) override;

        void setPrefabRigState(PreviewInstanceId instanceId, size_t part,
                               const std::string& stateName, float blendDuration) override;
        [[nodiscard]] std::vector<PrefabRigStateInfo> getPrefabRigStates(PreviewInstanceId instanceId,
                                                                         size_t part) const override;
        void setPrefabRigBool(PreviewInstanceId instanceId, size_t part,
                              const std::string& name, bool value) override;
        void setPrefabRigFloat(PreviewInstanceId instanceId, size_t part,
                               const std::string& name, float value) override;
        void setPrefabRigInt(PreviewInstanceId instanceId, size_t part,
                             const std::string& name, int32_t value) override;
        void setPrefabRigTrigger(PreviewInstanceId instanceId, size_t part, const std::string& name) override;
        void playPrefabRig(PreviewInstanceId instanceId) override;
        void pausePrefabRig(PreviewInstanceId instanceId) override;
        [[nodiscard]] bool isPrefabRigPaused(PreviewInstanceId instanceId) const override;

        void stepPrefabRigFrame(PreviewInstanceId instanceId, size_t part, int frames) override;
        void setPrefabRigNormalizedTime(PreviewInstanceId instanceId, size_t part, float t) override;
        [[nodiscard]] float getPrefabRigNormalizedTime(PreviewInstanceId instanceId, size_t part) const override;

        void setPrefabRigPartPreviewTransform(PreviewInstanceId instanceId, size_t part,
                                              const glm::mat4& transform) override;
        void resetPrefabRigPreviewTransforms(PreviewInstanceId instanceId) override;
        [[nodiscard]] glm::mat4 getPrefabRigPartWorld(PreviewInstanceId instanceId, size_t part) const override;
        [[nodiscard]] std::vector<PrefabRigJoint>
        getPrefabRigJointWorlds(PreviewInstanceId instanceId, size_t part) const override;

        [[nodiscard]] std::vector<animator::SocketDefinition>
        getPrefabRigSockets(PreviewInstanceId instanceId, size_t part) const override;
        void setPrefabRigSockets(PreviewInstanceId instanceId, size_t part,
                                 const std::vector<animator::SocketDefinition>& sockets) override;
        [[nodiscard]] std::vector<animator::ik::IKChainConfig>
        getPrefabRigChains(PreviewInstanceId instanceId) const override;
        void setPrefabRigChains(PreviewInstanceId instanceId,
                                const std::vector<animator::ik::IKChainConfig>& chains) override;

        // UI Layer Builder Preview (VK-1435)
        void initUILayerPreview(PreviewInstanceId instanceId) override;
        bool buildUILayerPreview(PreviewInstanceId instanceId, EntityHandle canvasRoot,
                                 uint32_t refWidth, uint32_t refHeight) override;
        void cleanUpUILayerPreview(PreviewInstanceId instanceId) override;
        [[nodiscard]] bool isUILayerPreviewBuilt(PreviewInstanceId instanceId) const override;

        void setUILayerReferenceResolution(PreviewInstanceId instanceId,
                                           uint32_t refWidth, uint32_t refHeight) override;

        [[nodiscard]] ViewportTextureHandle renderUILayerPreview(PreviewInstanceId instanceId) override;

        [[nodiscard]] EntityHandle pickUILayerElementAt(PreviewInstanceId instanceId,
                                                        glm::vec2 refPx) const override;
        [[nodiscard]] std::optional<UIResolvedRectData>
        getUILayerResolvedRect(PreviewInstanceId instanceId, EntityHandle entity) const override;

    private:
        void registerMaterialPreviewHandlers(::events::EventDispatcher& dispatcher);
        void registerMeshPreviewHandlers(::events::EventDispatcher& dispatcher);
        void registerAnimationPreviewHandlers(::events::EventDispatcher& dispatcher);
        void registerVFXPreviewHandlers(::events::EventDispatcher& dispatcher);
        void registerPrefabRigPreviewHandlers(::events::EventDispatcher& dispatcher);
        void registerUILayerPreviewHandlers(::events::EventDispatcher& dispatcher);
    };
}
