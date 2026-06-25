#pragma once

// VK-1433 — Core-side adapter for the Prefab Rig Preview provider.
//
// Owns one PrefabRigPreviewController (Layer B) per PreviewInstanceId and rebuilds the
// graphics-side controllers::PrefabRigDesc from the Editor-safe PrefabRigDescDTO that
// crosses the boundary. Mirrors AnimationPreviewAdapter exactly (same map/getController
// shape). The Editor never sees the controller or PrefabRigAssembly.hpp.

#include "../../services/providers/render/IPrefabRigPreviewProvider.hpp"
#include "../../graphics/controllers/preview/PrefabRigPreviewController.hpp"
#include <memory>
#include <unordered_map>

namespace core
{
    class PrefabRigPreviewAdapter : public services::IPrefabRigPreviewProvider
    {
    private:
        std::unordered_map<services::PreviewInstanceId,
                           std::unique_ptr<::controllers::PrefabRigPreviewController>> controllers;

    public:
        explicit PrefabRigPreviewAdapter() = default;
        ~PrefabRigPreviewAdapter() noexcept override;

        void initPrefabRigPreview(services::PreviewInstanceId instanceId) override;
        bool buildPrefabRigPreview(services::PreviewInstanceId instanceId,
                                   const services::PrefabRigDescDTO& desc) override;
        bool updatePrefabRigPreviewTransforms(services::PreviewInstanceId instanceId,
                                              const services::PrefabRigDescDTO& desc) override;
        void cleanUpPrefabRigPreview(services::PreviewInstanceId instanceId) override;
        bool isPrefabRigPreviewBuilt(services::PreviewInstanceId instanceId) const override;
        size_t getPrefabRigPartCount(services::PreviewInstanceId instanceId) const override;

        void updatePrefabRigPreview(services::PreviewInstanceId instanceId, float deltaTime) override;
        void updatePrefabRigCamera(services::PreviewInstanceId instanceId, const glm::mat4& view,
                                   const glm::mat4& projection, const glm::vec3& cameraPos) override;
        void setPrefabRigEnvironment(services::PreviewInstanceId instanceId,
                                     const services::PreviewEnvironmentParams& params) override;
        void setPrefabRigRootMatrix(services::PreviewInstanceId instanceId, const glm::mat4& model) override;

        void* renderPrefabRigPreview(services::PreviewInstanceId instanceId) override;

        void setPrefabRigState(services::PreviewInstanceId instanceId, size_t part,
                               const std::string& stateName, float blendDuration) override;
        std::vector<services::PrefabRigStateInfo> getPrefabRigStates(services::PreviewInstanceId instanceId,
                                                                     size_t part) const override;
        void setPrefabRigBool(services::PreviewInstanceId instanceId, size_t part,
                              const std::string& name, bool value) override;
        void setPrefabRigFloat(services::PreviewInstanceId instanceId, size_t part,
                               const std::string& name, float value) override;
        void setPrefabRigInt(services::PreviewInstanceId instanceId, size_t part,
                             const std::string& name, int32_t value) override;
        void setPrefabRigTrigger(services::PreviewInstanceId instanceId, size_t part,
                                 const std::string& name) override;
        void playPrefabRig(services::PreviewInstanceId instanceId) override;
        void pausePrefabRig(services::PreviewInstanceId instanceId) override;
        bool isPrefabRigPaused(services::PreviewInstanceId instanceId) const override;

        void stepPrefabRigFrame(services::PreviewInstanceId instanceId, size_t part, int frames) override;
        void setPrefabRigNormalizedTime(services::PreviewInstanceId instanceId, size_t part, float t) override;
        float getPrefabRigNormalizedTime(services::PreviewInstanceId instanceId, size_t part) const override;

        void setPrefabRigPartPreviewTransform(services::PreviewInstanceId instanceId, size_t part,
                                              const glm::mat4& transform) override;
        void resetPrefabRigPreviewTransforms(services::PreviewInstanceId instanceId) override;
        glm::mat4 getPrefabRigPartWorld(services::PreviewInstanceId instanceId, size_t part) const override;
        std::vector<services::PrefabRigJoint> getPrefabRigJointWorlds(services::PreviewInstanceId instanceId,
                                                                      size_t part) const override;

        std::vector<animator::SocketDefinition> getPrefabRigSockets(services::PreviewInstanceId instanceId,
                                                                    size_t part) const override;
        void setPrefabRigSockets(services::PreviewInstanceId instanceId, size_t part,
                                 const std::vector<animator::SocketDefinition>& sockets) override;
        std::vector<animator::ik::IKChainConfig> getPrefabRigChains(
            services::PreviewInstanceId instanceId) const override;
        void setPrefabRigChains(services::PreviewInstanceId instanceId,
                                const std::vector<animator::ik::IKChainConfig>& chains) override;

    private:
        controllers::PrefabRigPreviewController* getController(services::PreviewInstanceId instanceId) const;
        static ::controllers::PrefabRigDesc toDesc(const services::PrefabRigDescDTO& dto);
    };
}
