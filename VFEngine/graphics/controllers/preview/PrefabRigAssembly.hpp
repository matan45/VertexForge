#pragma once

// VK-1433 Layer A — Prefab Rig Preview, standalone entt-free assembly.
//
// Given an in-memory PrefabRigDesc (parsed from a .vfPrefab by the editor window),
// PrefabRigAssembly assembles a multi-part character rig and advances it every
// frame using the engine's PURE solvers — the SAME math/order as main-scene Play,
// but WITHOUT ever touching the shared scene::EntityRegistry. No entt entity is
// ever created here (the isolation invariant of the ticket).
//
// The per-frame update order mirrors Play exactly (line refs pinned in the .cpp):
//   1. each skeletal part: animator->update(dt)  (or evaluateRestPose() if paused)
//   2. resolve the attachment chain in topological (parent-before-child) order,
//      composing each child's partWorld from its parent's socket world transform.
//   3. each IK chain: feed the runtime target from the bound part's grip socket.
//   4. each body part with chains: IKPostProcessor::applyIK on its bone matrices.
//
// The pure math is factored into the free functions in namespace prefabrig below so
// it is unit-testable on the CPU with hand-built SkeletonData/SocketDefinition
// inputs (no AnimationLayerStack, no Vulkan, no assets) — see
// tests/test_prefab_rig_assembly.cpp.

#include "../../animation/AnimationLayerStack.hpp"
#include "../../animation/AnimationDataCache.hpp"
#include "../../animation/RetargetContext.hpp"
#include "animator/SocketTypes.hpp"
#include "animator/IKTypes.hpp"
#include "components/IKComponent.hpp"
#include "resource/Types.hpp"

#include <glm/glm.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace controllers
{
    // ----------------------------------------------------------------------
    // Input description (asset-agnostic; built by the window from prefab JSON).
    // ----------------------------------------------------------------------

    struct PrefabRigPart
    {
        std::string meshPath;           // .vfMesh (skeletal or static)
        std::string animatorPath;       // empty => static part (no animator)
        std::string retargetPath;       // empty => no retarget (.vfretarget map)

        int parentPartIndex = -1;       // -1 => root part
        std::string attachParentSocket; // bone/static socket name on the parent (empty for roots)

        // From the SocketAttachment-driven child transform. The translation is
        // intentionally NOT carried: the child rides the socket origin, matching
        // SocketAttachmentUpdater::applyModelOffset (translation dropped).
        glm::vec3 attachChildRotation{0.0f}; // degrees (Euler), as TransformComponent.rotation
        glm::vec3 attachChildScale{1.0f};

        // VK-1433 Layer B (rendering) material references, populated by the editor window
        // from the prefab's MaterialComponent. PrefabRigAssembly's simulation is material-
        // free and ignores these entirely; only the preview controller (Layer B) reads them.
        std::string defaultMaterialPath;                       // .vfMat/.vfMatInstance for the whole part
        std::map<std::string, std::string> subMeshMaterials;   // submesh name -> material path override
    };

    struct PrefabRigIK
    {
        animator::ik::IKChainConfig chain; // chainName/tipBoneName/chainBoneNames/constraints/weight/enabled

        int bodyPartIndex = -1;            // which skeletal part owns (solves) the chain
        int targetPartIndex = -1;          // part whose socket drives the target (editor-transient binding)
        std::string targetSocketName;      // grip socket name on the target part (editor-transient binding)
    };

    struct PrefabRigDesc
    {
        std::vector<PrefabRigPart> parts;
        std::vector<PrefabRigIK> ik;
    };

    // ----------------------------------------------------------------------
    // Pure, CPU-testable math (no AnimationLayerStack / Vulkan / assets).
    // Each function mirrors a verified Play formula; the comment pins the source.
    // ----------------------------------------------------------------------
    namespace prefabrig
    {
        // Bone-socket model transform. Mirrors AnimationLayerStack::computeSocketTransforms
        // (AnimationLayerStackControls.cpp:208-228) for one socket:
        //   boneMeshPos = (boneMatrices[boneIdx] * bindPoses[boneIdx] * vec4(0,0,0,1)).xyz
        //   out = translate(boneMeshPos + socket.localPosition) * mat4_cast(socket.localRotation)
        // Returns identity when boneIndex is out of range for either array (matches the
        // else-branch at AnimationLayerStackControls.cpp:226).
        glm::mat4 socketModelTransform(const std::vector<glm::mat4>& boneMatrices,
                                       const std::vector<glm::mat4>& bindPoses,
                                       const animator::SocketDefinition& socket);

        // Child world transform from a resolved socket world transform. Mirrors
        // SocketAttachmentUpdater::applyModelOffset (SocketAttachmentUpdater.cpp:308-318):
        //   entityLocal = mat4_cast(quat(radians(rotationDeg))) * scale(scaleVec)
        //   childWorld  = socketWorld * entityLocal       (the socket TRANSLATION is the
        //                                                   only positional contribution —
        //                                                   the child's own translation is
        //                                                   intentionally dropped)
        glm::mat4 composeChildWorld(const glm::mat4& socketWorld,
                                    const glm::vec3& childRotationDeg,
                                    const glm::vec3& childScale);

        // World-space IK target origin from a grip socket on the target part. Mirrors the
        // Play feed: gripWorld = targetPartWorld * grip.getLocalOffsetMatrix(); the target
        // POSITION fed to the solver is that matrix's translation column.
        glm::vec3 ikTargetFromSocket(const glm::mat4& targetPartWorld,
                                     const animator::SocketDefinition& gripSocket);
    }

    // ----------------------------------------------------------------------
    // PrefabRigAssembly — owns the per-part state and advances it each frame.
    // ----------------------------------------------------------------------
    class PrefabRigAssembly
    {
    public:
        PrefabRigAssembly();
        ~PrefabRigAssembly();

        PrefabRigAssembly(const PrefabRigAssembly&) = delete;
        PrefabRigAssembly& operator=(const PrefabRigAssembly&) = delete;
        PrefabRigAssembly(PrefabRigAssembly&&) = delete;            // holds self-referential pointers
        PrefabRigAssembly& operator=(PrefabRigAssembly&&) = delete;

        // Loads skeletons/sockets/animators from disk (via AnimationDataCache) and builds
        // one AnimationLayerStack per skeletal part. Returns false (and logs) if no part
        // could be built; partially-built parts are kept (a static-only part is valid).
        // Re-calling rebuilds from scratch.
        bool build(const PrefabRigDesc& desc);

        // Advances animators, resolves attachments (topological), feeds + applies IK.
        // Order identical to Play (see the .cpp). No-op if not built.
        void update(float dt);

        bool isBuilt() const { return built; }
        size_t partCount() const { return parts.size(); }

        // --- Accessors for Layer B (rendering) ------------------------------
        // Composed (post-IK) bone matrices for a skeletal part; empty for static parts
        // or an out-of-range index.
        const std::vector<glm::mat4>& boneMatrices(size_t part) const;
        // Model->world matrix for a part (root = preview/turntable model matrix).
        glm::mat4 partWorld(size_t part) const;
        // Mesh path of a part (for the renderer to load geometry), "" if out of range.
        const std::string& meshPath(size_t part) const;
        bool isSkeletalPart(size_t part) const;

        // --- Authoring accessors (live edit, no disk round-trip) ------------
        // Mutating these is reflected on the next update(): editable bone/static sockets
        // per part, and the IK chain configs. Returns a reference to an empty static
        // vector for an out-of-range part (never dangles).
        std::vector<animator::SocketDefinition>& editableSockets(size_t part);
        const std::vector<animator::SocketDefinition>& editableSockets(size_t part) const;
        std::vector<animator::ik::IKChainConfig>& editableChains();
        const std::vector<animator::ik::IKChainConfig>& editableChains() const;

        // --- State / parameter control (pass-throughs) ----------------------
        // Force a state on a skeletal part's animator (state picker). No-op if the part
        // is static / out of range.
        bool forceState(size_t part, const std::string& stateName, float blendDuration = 0.25f);
        // Animator data for the state picker (graph.states); nullptr if static/out of range.
        const animator::AnimatorData* animatorData(size_t part) const;

        void setBool(size_t part, const std::string& name, bool value);
        void setFloat(size_t part, const std::string& name, float value);
        void setInt(size_t part, const std::string& name, int32_t value);
        void setTrigger(size_t part, const std::string& name);

        // Playback: paused parts hold their evaluated pose (evaluateRestPose) instead of
        // advancing time. Applies to every skeletal part.
        void play();
        void pause();
        bool isPaused() const { return paused; }

        // Root (turntable) model matrix applied to root parts; propagates through the
        // attachment chain so a non-identity preview rotation still feeds IK correctly.
        void setRootModelMatrix(const glm::mat4& m) { rootModelMatrix = m; }
        const glm::mat4& rootModelMatrix_() const { return rootModelMatrix; }

    private:
        // Per-part state. SkeletonData is owned BY VALUE (a copy of the cache entry) so it
        // outlives `stack` (which holds &skeleton) and so editable sockets are independent
        // of the shared cache. retarget is owned per part and outlives `stack` too.
        struct Part
        {
            std::string meshPath;
            std::string animatorPath;
            bool skeletal = false;

            resource::SkeletonData skeleton;                        // by value — outlives stack
            std::shared_ptr<animator::AnimatorData> animatorData;   // kept alive for the stack
            std::unique_ptr<animation::RetargetContext> retarget;   // null => native skeleton
            std::unique_ptr<animation::AnimationLayerStack> stack;  // built last, destroyed first

            std::vector<animator::SocketDefinition> sockets;        // editable authoring copy

            int parentIndex = -1;
            int parentSocketIndex = -1;                             // index into parent.sockets, -1 if absent
            glm::vec3 attachRotationDeg{0.0f};
            glm::vec3 attachScale{1.0f};

            glm::mat4 partWorld{1.0f};

            // Identity bone set for static parts (Layer B feeds this to the skinned pipeline).
            std::vector<glm::mat4> identityBones;
        };

        // One runtime state per IK chain, plus the resolved part/socket binding.
        struct Chain
        {
            int bodyPartIndex = -1;
            int targetPartIndex = -1;
            int targetSocketIndex = -1;                             // index into target part's sockets
            components::IKChainRuntimeState runtimeState;
        };

        void clear();
        void resolveTopologicalOrder();
        // Builds a RetargetContext for a skeletal part from a .vfretarget map path. Mirrors
        // RuntimeAnimatorSystem::buildEntityRetargetContext. nullptr if unavailable/empty.
        std::unique_ptr<animation::RetargetContext> buildRetargetForPart(const Part& part,
                                                                         const std::string& retargetPath);

        // Cache MUST outlive every stack: it owns the skeleton/animation data the stacks
        // reference through the load callback. Declared FIRST so it is destroyed LAST.
        animation::AnimationDataCache dataCache;

        std::vector<Part> parts;
        std::vector<animator::ik::IKChainConfig> chainConfigs; // editable; parallel to `chains`
        std::vector<Chain> chains;
        std::vector<size_t> topoOrder;                         // part indices, parents before children

        glm::mat4 rootModelMatrix{1.0f};
        bool built = false;
        bool paused = false;

        // Returned for out-of-range accessors so callers never dangle.
        std::vector<glm::mat4> emptyMatrices;
        std::vector<animator::SocketDefinition> emptySockets;
    };
}
