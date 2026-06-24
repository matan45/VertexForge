#pragma once
#include "../../providers/render/IPreviewProvider.hpp"
#include "../../providers/render/IPrefabRigPreviewProvider.hpp"
#include "../../providers/animation/IAnimationPreviewProvider.hpp"
#include "../../providers/vfx/IVFXPreviewProvider.hpp"
#include "../../data/DTOs.hpp"
#include "../../data/PrefabRigDescDTO.hpp"
#include "../../data/AsyncLoadingTypes.hpp"
#include <math/Frustum.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

namespace services
{
    class IPreviewService
    {
    public:
        virtual ~IPreviewService() = default;

        virtual void registerEventHandlers() = 0;

        virtual void initMaterialPreview(PreviewInstanceId instanceId) = 0;

        virtual void cleanUpMaterialPreview(PreviewInstanceId instanceId) = 0;

        virtual void setMaterialParams(PreviewInstanceId instanceId, const MaterialPreviewParams& params) = 0;

        virtual void updateMaterialCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                          const glm::mat4& projection,
                                          const glm::vec3& cameraPos, float time = 0.0f) = 0;

        [[nodiscard]] virtual ViewportTextureHandle renderMaterialPreview(PreviewInstanceId instanceId) = 0;

        [[nodiscard]] virtual std::string getMaterialShaderError(PreviewInstanceId instanceId) const = 0;

        virtual void initMeshPreview(PreviewInstanceId instanceId) = 0;

        virtual void cleanUpMeshPreview(PreviewInstanceId instanceId) = 0;

        [[nodiscard]] virtual std::vector<SubMeshInfo> getPreviewMeshSubMeshInfo(PreviewInstanceId instanceId) const =
        0;

        [[nodiscard]] virtual std::vector<LODInfo> getPreviewMeshLODInfo(PreviewInstanceId instanceId) const = 0;

        [[nodiscard]] virtual math::AABB getPreviewMeshBounds(PreviewInstanceId instanceId) const = 0;

        virtual void setMeshPreviewParams(PreviewInstanceId instanceId, const MeshPreviewParams& params) = 0;

        virtual void setPreviewEnvironment(PreviewInstanceId instanceId, const PreviewEnvironmentParams& params) = 0;

        virtual void updateMeshCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                      const glm::vec3& cameraPos) = 0;

        [[nodiscard]] virtual ViewportTextureHandle renderMeshPreview(PreviewInstanceId instanceId) = 0;

        // Async mesh loading
        virtual void loadPreviewMeshAsync(PreviewInstanceId instanceId, const std::string& meshPath) = 0;
        virtual void cancelMeshLoading(PreviewInstanceId instanceId) = 0;
        [[nodiscard]] virtual MeshLoadingProgress getMeshLoadingProgress(PreviewInstanceId instanceId) const = 0;
        virtual void processAsyncLoading() = 0;

        // Animation Preview
        virtual void initAnimationPreview(PreviewInstanceId instanceId) = 0;
        virtual void cleanUpAnimationPreview(PreviewInstanceId instanceId) = 0;
        virtual bool loadAnimationPreviewMesh(PreviewInstanceId instanceId, const std::string& meshPath) = 0;
        virtual bool loadAnimationPreviewAnimation(PreviewInstanceId instanceId, const std::string& animPath) = 0;

        virtual void playAnimation(PreviewInstanceId instanceId) = 0;
        virtual void pauseAnimation(PreviewInstanceId instanceId) = 0;
        virtual void stopAnimation(PreviewInstanceId instanceId) = 0;
        [[nodiscard]] virtual bool isAnimationPlaying(PreviewInstanceId instanceId) const = 0;

        virtual void setAnimationPlaybackTime(PreviewInstanceId instanceId, float timeSeconds) = 0;
        [[nodiscard]] virtual float getAnimationPlaybackTime(PreviewInstanceId instanceId) const = 0;

        virtual void setAnimationLooping(PreviewInstanceId instanceId, bool loop) = 0;
        virtual void setAnimationPlaybackSpeed(PreviewInstanceId instanceId, float speed) = 0;

        virtual void updateAnimationPreview(PreviewInstanceId instanceId, float deltaTime) = 0;
        virtual void setAnimationPreviewParams(PreviewInstanceId instanceId, const AnimationPreviewParams& params) = 0;
        virtual void setAnimationPreviewEnvironment(PreviewInstanceId instanceId,
                                                    const PreviewEnvironmentParams& params) = 0;
        virtual void updateAnimationCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                           const glm::mat4& projection, const glm::vec3& cameraPos) = 0;

        [[nodiscard]] virtual ViewportTextureHandle renderAnimationPreview(PreviewInstanceId instanceId) = 0;
        [[nodiscard]] virtual std::vector<EvaluatedBoneInfo>
        getAnimationPreviewEvaluatedBones(PreviewInstanceId instanceId) const = 0;

        // VFX Preview
        virtual void initVFXPreview(PreviewInstanceId instanceId) = 0;
        virtual void cleanUpVFXPreview(PreviewInstanceId instanceId) = 0;

        virtual void setVFXParams(PreviewInstanceId instanceId, const VFXPreviewParams& params) = 0;

        virtual void updateVFXCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                     const glm::mat4& projection, const glm::vec3& cameraPos, float time) = 0;
        virtual void updateVFXSimulation(PreviewInstanceId instanceId, float deltaTime) = 0;

        virtual void playVFX(PreviewInstanceId instanceId) = 0;
        virtual void pauseVFX(PreviewInstanceId instanceId) = 0;
        virtual void stopVFX(PreviewInstanceId instanceId) = 0;

        [[nodiscard]] virtual ViewportTextureHandle renderVFXPreview(PreviewInstanceId instanceId) = 0;

        // Prefab Rig Preview (VK-1433) — assembled multi-part rig, entt-free.
        virtual void initPrefabRigPreview(PreviewInstanceId instanceId) = 0;
        virtual bool buildPrefabRigPreview(PreviewInstanceId instanceId, const PrefabRigDescDTO& desc) = 0;
        virtual void cleanUpPrefabRigPreview(PreviewInstanceId instanceId) = 0;
        [[nodiscard]] virtual bool isPrefabRigPreviewBuilt(PreviewInstanceId instanceId) const = 0;
        [[nodiscard]] virtual size_t getPrefabRigPartCount(PreviewInstanceId instanceId) const = 0;

        virtual void updatePrefabRigPreview(PreviewInstanceId instanceId, float deltaTime) = 0;
        virtual void updatePrefabRigCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                           const glm::mat4& projection, const glm::vec3& cameraPos) = 0;
        virtual void setPrefabRigEnvironment(PreviewInstanceId instanceId,
                                             const PreviewEnvironmentParams& params) = 0;
        virtual void setPrefabRigRootMatrix(PreviewInstanceId instanceId, const glm::mat4& model) = 0;

        [[nodiscard]] virtual ViewportTextureHandle renderPrefabRigPreview(PreviewInstanceId instanceId) = 0;

        virtual void setPrefabRigState(PreviewInstanceId instanceId, size_t part,
                                       const std::string& stateName, float blendDuration) = 0;
        [[nodiscard]] virtual std::vector<PrefabRigStateInfo> getPrefabRigStates(PreviewInstanceId instanceId,
                                                                                 size_t part) const = 0;
        virtual void setPrefabRigBool(PreviewInstanceId instanceId, size_t part,
                                      const std::string& name, bool value) = 0;
        virtual void setPrefabRigFloat(PreviewInstanceId instanceId, size_t part,
                                       const std::string& name, float value) = 0;
        virtual void setPrefabRigInt(PreviewInstanceId instanceId, size_t part,
                                     const std::string& name, int32_t value) = 0;
        virtual void setPrefabRigTrigger(PreviewInstanceId instanceId, size_t part,
                                         const std::string& name) = 0;
        virtual void playPrefabRig(PreviewInstanceId instanceId) = 0;
        virtual void pausePrefabRig(PreviewInstanceId instanceId) = 0;
        [[nodiscard]] virtual bool isPrefabRigPaused(PreviewInstanceId instanceId) const = 0;

        // VK-1433 — frame-by-frame scrub (editor-transient).
        virtual void stepPrefabRigFrame(PreviewInstanceId instanceId, size_t part, int frames) = 0;
        virtual void setPrefabRigNormalizedTime(PreviewInstanceId instanceId, size_t part, float t) = 0;
        [[nodiscard]] virtual float getPrefabRigNormalizedTime(PreviewInstanceId instanceId, size_t part) const = 0;

        // VK-1433 — transform gizmo (editor-transient, never serialized) + live part-world anchor.
        virtual void setPrefabRigPartPreviewTransform(PreviewInstanceId instanceId, size_t part,
                                                      const glm::mat4& transform) = 0;
        virtual void resetPrefabRigPreviewTransforms(PreviewInstanceId instanceId) = 0;
        [[nodiscard]] virtual glm::mat4 getPrefabRigPartWorld(PreviewInstanceId instanceId, size_t part) const = 0;

        // VK-1433 Phase 1b — world-space joints of a skeletal part (editor bone-picking).
        [[nodiscard]] virtual std::vector<PrefabRigJoint>
        getPrefabRigJointWorlds(PreviewInstanceId instanceId, size_t part) const = 0;

        [[nodiscard]] virtual std::vector<animator::SocketDefinition>
        getPrefabRigSockets(PreviewInstanceId instanceId, size_t part) const = 0;
        virtual void setPrefabRigSockets(PreviewInstanceId instanceId, size_t part,
                                         const std::vector<animator::SocketDefinition>& sockets) = 0;
        [[nodiscard]] virtual std::vector<animator::ik::IKChainConfig>
        getPrefabRigChains(PreviewInstanceId instanceId) const = 0;
        virtual void setPrefabRigChains(PreviewInstanceId instanceId,
                                        const std::vector<animator::ik::IKChainConfig>& chains) = 0;

        // UI Layer Builder Preview (VK-1435) — offscreen WYSIWYG canvas preview. Element
        // edits do NOT go through here; the builder authors live entities via the existing
        // UIComponentService CQRS. These cover only the offscreen render + reference-extent
        // hit-test / resolved-rect.
        virtual void initUILayerPreview(PreviewInstanceId instanceId) = 0;
        virtual bool buildUILayerPreview(PreviewInstanceId instanceId, EntityHandle canvasRoot,
                                         uint32_t refWidth, uint32_t refHeight) = 0;
        virtual void cleanUpUILayerPreview(PreviewInstanceId instanceId) = 0;
        [[nodiscard]] virtual bool isUILayerPreviewBuilt(PreviewInstanceId instanceId) const = 0;

        virtual void setUILayerReferenceResolution(PreviewInstanceId instanceId,
                                                   uint32_t refWidth, uint32_t refHeight) = 0;

        [[nodiscard]] virtual ViewportTextureHandle renderUILayerPreview(PreviewInstanceId instanceId) = 0;

        [[nodiscard]] virtual EntityHandle pickUILayerElementAt(PreviewInstanceId instanceId,
                                                                glm::vec2 refPx) const = 0;
        [[nodiscard]] virtual std::optional<UIResolvedRectData>
        getUILayerResolvedRect(PreviewInstanceId instanceId, EntityHandle entity) const = 0;
    };
}
