#pragma once

// VK-1433 — Prefab Rig Preview provider interface.
//
// Abstracts the multi-part assembled-rig preview (Layer B PrefabRigPreviewController)
// for the editor, keyed by PreviewInstanceId so each open window owns an independent
// controller. The Core-side PrefabRigPreviewAdapter implements this by owning an
// unordered_map<PreviewInstanceId, unique_ptr<PrefabRigPreviewController>> and rebuilding
// the real PrefabRigDesc from the PrefabRigDescDTO that crosses here.
//
// Everything below uses only Editor-safe types (PrefabRigDescDTO, glm, and the
// utilities/animator socket/IK structs the editor already consumes).

#include <glm/glm.hpp>
#include "../PreviewInstanceId.hpp"
#include "IMeshPreviewProvider.hpp" // PreviewEnvironmentParams
#include "../../data/PrefabRigDescDTO.hpp"
#include "animator/SocketTypes.hpp"
#include "animator/IKTypes.hpp"
#include <string>
#include <vector>

namespace services
{
    // A single animator state name for the state picker (mirrors animator::AnimatorState.name).
    struct PrefabRigStateInfo
    {
        std::string name;
    };

    // VK-1433 Phase 1b — one skeletal joint exposed for editor bone-picking. `world` is the joint's
    // world-space origin (the SAME formula the skeleton overlay draws:
    //   partWorld * boneMatrices[boneIndex] * bindPoses[boneIndex] * (0,0,0,1)), `boneName`/`boneIndex`
    // are taken straight from the part's SkeletonData so the window can prefill a SocketDefinition
    // after a screen-space pick. Editor-safe (glm + std::string only); no Graphics types cross.
    struct PrefabRigJoint
    {
        glm::vec3 world{0.0f};
        std::string boneName;
        int32_t boneIndex = -1;
    };

    class IPrefabRigPreviewProvider
    {
    public:
        virtual ~IPrefabRigPreviewProvider() = default;

        // Lifecycle. initPrefabRigPreview creates + inits the controller for this instance;
        // buildPrefabRigPreview (re)assembles the rig from the description and stands up the
        // per-part pipelines. cleanUpPrefabRigPreview tears the whole instance down.
        virtual void initPrefabRigPreview(PreviewInstanceId instanceId) = 0;
        virtual bool buildPrefabRigPreview(PreviewInstanceId instanceId, const PrefabRigDescDTO& desc) = 0;
        // Cheap transform-only update of an already-built preview (no mesh/skeleton/texture reload).
        // Returns false on structural drift so the caller falls back to buildPrefabRigPreview.
        virtual bool updatePrefabRigPreviewTransforms(PreviewInstanceId instanceId,
                                                      const PrefabRigDescDTO& desc) = 0;
        virtual void cleanUpPrefabRigPreview(PreviewInstanceId instanceId) = 0;
        virtual bool isPrefabRigPreviewBuilt(PreviewInstanceId instanceId) const = 0;
        virtual size_t getPrefabRigPartCount(PreviewInstanceId instanceId) const = 0;

        // Per-frame simulation + camera + environment + turntable.
        virtual void updatePrefabRigPreview(PreviewInstanceId instanceId, float deltaTime) = 0;
        virtual void updatePrefabRigCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                           const glm::mat4& projection, const glm::vec3& cameraPos) = 0;
        virtual void setPrefabRigEnvironment(PreviewInstanceId instanceId,
                                             const PreviewEnvironmentParams& params) = 0;
        virtual void setPrefabRigRootMatrix(PreviewInstanceId instanceId, const glm::mat4& model) = 0;

        // Renders all parts to one image; returns the ImGui descriptor set (or nullptr).
        virtual void* renderPrefabRigPreview(PreviewInstanceId instanceId) = 0;

        // State / parameter control (state picker + idle/run/fire drivers).
        virtual void setPrefabRigState(PreviewInstanceId instanceId, size_t part,
                                       const std::string& stateName, float blendDuration) = 0;
        virtual std::vector<PrefabRigStateInfo> getPrefabRigStates(PreviewInstanceId instanceId,
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
        virtual bool isPrefabRigPaused(PreviewInstanceId instanceId) const = 0;

        // Frame-by-frame scrub (VK-1433): step a skeletal part by ±frames (even while paused), seek
        // it to a normalized [0,1] position, or read its current normalized time. Editor-transient.
        virtual void stepPrefabRigFrame(PreviewInstanceId instanceId, size_t part, int frames) = 0;
        virtual void setPrefabRigNormalizedTime(PreviewInstanceId instanceId, size_t part, float t) = 0;
        virtual float getPrefabRigNormalizedTime(PreviewInstanceId instanceId, size_t part) const = 0;

        // Transform gizmo (VK-1433, EDITOR-TRANSIENT, never serialized): set a part's preview
        // transform (root part moves the whole rig), reset all to identity, and read the part's
        // LIVE composed world matrix to anchor the gizmo (also used by the static-socket gizmo).
        virtual void setPrefabRigPartPreviewTransform(PreviewInstanceId instanceId, size_t part,
                                                      const glm::mat4& transform) = 0;
        virtual void resetPrefabRigPreviewTransforms(PreviewInstanceId instanceId) = 0;
        virtual glm::mat4 getPrefabRigPartWorld(PreviewInstanceId instanceId, size_t part) const = 0;

        // VK-1433 Phase 1b — world-space joints of a skeletal part for editor bone-picking. One
        // entry per valid bone (skeletal parts only); empty for static / out-of-range parts. The
        // window projects these to screen and hit-tests a click to prefill a bone socket.
        virtual std::vector<PrefabRigJoint> getPrefabRigJointWorlds(PreviewInstanceId instanceId,
                                                                    size_t part) const = 0;

        // (Skeletal-vs-static and mesh path are known to the window from the DTO it built,
        //  so they are not re-queried across the boundary.)

        // Live authoring: copy the controller's editable sockets/chains out for the panels,
        // push the panel-edited copy back so the next frame re-resolves (no disk round-trip).
        virtual std::vector<animator::SocketDefinition> getPrefabRigSockets(PreviewInstanceId instanceId,
                                                                            size_t part) const = 0;
        virtual void setPrefabRigSockets(PreviewInstanceId instanceId, size_t part,
                                         const std::vector<animator::SocketDefinition>& sockets) = 0;
        virtual std::vector<animator::ik::IKChainConfig> getPrefabRigChains(
            PreviewInstanceId instanceId) const = 0;
        virtual void setPrefabRigChains(PreviewInstanceId instanceId,
                                        const std::vector<animator::ik::IKChainConfig>& chains) = 0;
    };
}
