#pragma once

#include "AnimationExport.hpp"
#include "AnimatorStateMachine.hpp"
#include "AnimationLayerStack.hpp"
#include "AnimationLOD.hpp"
#include "AnimationDataCache.hpp"
#include "RetargetContext.hpp"
#include "SocketAttachmentUpdater.hpp"
#include "../../services/data/AnimationAuthoringData.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/animation/AnimationSnapshotEvents.hpp"
#include "asset/AssetGUID.hpp"
#include "math/Frustum.hpp"
#include <entt/entt.hpp>
#include <atomic>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace components
{
    struct MeshComponent;
}

namespace animation
{
    struct AnimationCullingContext
    {
        math::Frustum frustum;
        glm::vec3 cameraPos{0.0f};
        bool enabled = false;
    };

#pragma warning(push)
#pragma warning(disable: 4251)
    class VF_ANIMATION_API RuntimeAnimatorSystem
    {
    private:
        AnimationDataCache dataCache;

        std::unordered_map<entt::entity, std::unique_ptr<AnimationLayerStack>> animators;
        // Owned retarget contexts, parallel to animators (VK-910). Referenced by raw
        // pointer inside each layer stack's evaluators, so must outlive them.
        std::unordered_map<entt::entity, std::unique_ptr<RetargetContext>> retargetContexts;

        // What each animator was built from (VK-1656). syncWithRegistry() rebuilds an animator
        // whose MeshComponent now names other assets (animator A->B, mesh or retarget swap).
        struct AnimatorSources
        {
            asset::AssetGUID animator;
            asset::AssetGUID mesh;
            asset::AssetGUID retarget;
        };
        std::unordered_map<entt::entity, AnimatorSources> animatorSources;

        // Edit-mode preview of a state or clip on a stack separate from the entity's animator
        // (VK-1656). Members are destroyed bottom-up: the stack holds raw pointers to `data`
        // and `retarget`, and to the cached skeleton and clips.
        struct EditPreviewSession
        {
            services::AnimationEditPreviewRequest request;
            std::unique_ptr<animator::AnimatorData> data; // synthetic one-state graph
            std::unique_ptr<RetargetContext> retarget;
            std::unique_ptr<AnimationLayerStack> stack;
            asset::AssetGUID animator; // State mode: the animator the state was copied from
            asset::AssetGUID mesh;
            float durationSeconds = 0.0f;
        };
        std::unordered_map<entt::entity, EditPreviewSession> editPreviews;

        // Requests from any thread (VK-1656). Animation.dll's EventDispatcher is a private copy,
        // so AnimatorSystemController forwards the EXE's notifications into this queue; it is
        // applied on the main thread at the top of syncWithRegistry()/updateEditModePreview().
        struct PendingRequests
        {
            std::vector<std::string> animatorPaths;
            std::vector<std::string> meshPaths;
            bool modeTransition = false;
        };
        PendingRequests pendingRequests;
        std::mutex pendingRequestMutex;
        std::atomic<bool> hasPendingRequests{false};

        std::unique_ptr<SocketAttachmentUpdater> socketUpdater;

        AnimationCullingContext cullingContext;
        AnimationLODManager lodManager;
        std::unordered_map<entt::entity, EntityAnimationLODState> entityLODStates;
        uint32_t culledEntityCount = 0;

        struct PendingAnimatorInit
        {
            entt::entity entity;
            std::string animatorPath;
        };
        std::vector<PendingAnimatorInit> pendingInitQueue;
        mutable std::mutex pendingInitMutex;
        uint32_t maxInitPerFrame = 4;

        events::SubscriptionToken sectorLoadedToken;
        events::SubscriptionToken sectorUnloadedToken;

        bool initialized = false;
        // Atomic: re-armed by the request API above and by sector load/unload notifications,
        // which may arrive off the main thread, and read in updateEditModePreview() on the main thread.
        std::atomic<bool> editPreviewDirty{true};

        // Pending animation state restores (keyed by entity UUID, consumed after animator init)
        std::unordered_map<uint64_t, events::animation::snapshot::AnimationSnapshot> pendingRestores;

        struct AnimationInstanceGroup
        {
            entt::entity leader = entt::null;
            std::vector<entt::entity> followers;
        };

        std::unordered_map<uint64_t, AnimationInstanceGroup> instanceGroups;
        uint32_t activeInstanceGroupCount = 0;

    public:
        static RuntimeAnimatorSystem& instance();

        void initialize();
        void shutdown();

        void setCullingContext(const math::Frustum& frustum, const glm::vec3& cameraPos);
        void clearCullingContext();
        uint32_t getCulledEntityCount() const { return culledEntityCount; }
        const AnimationCullingContext& getCullingContext() const { return cullingContext; }

        void updateAll(float deltaTime);
        void updateSocketAttachments();

        // Edit-mode (no Play) preview: sync animators and evaluate each to a static rest pose
        // (+IK) so socket transforms resolve and skeletal meshes render in rest pose, or to its
        // edit preview session's pose, advanced by deltaTime while playing (VK-1656). Dirty-gated
        // (one bool check when idle) unless a preview session is active. VK-1407.
        void updateEditModePreview(float deltaTime);

        // Thread-safe requests (any thread), applied on the main thread at the next
        // syncWithRegistry()/updateEditModePreview(). VK-1656.
        void requestAnimatorReload(std::string path); // absolute .vfAnimator path that was saved
        void requestMeshReload(std::string path);     // absolute .vfMesh path whose sockets/IK chains were saved
        void requestModeTransition();                 // Play <-> Edit: ends every edit preview
        void markEditPreviewDirty();

        // Edit-mode preview (VK-1656). Main thread only, in edit mode (the MCP drain). Plays or
        // poses a state of the entity's own animator (base layer), or any clip, on a stack
        // separate from the animator; the animator itself is untouched. Replaces the entity's
        // current preview.
        services::AnimationEditPreviewResult startEditPreview(entt::entity entity,
                                                              const services::AnimationEditPreviewRequest& request);
        bool stopEditPreview(entt::entity entity); // true if one was stopped
        bool stopAllEditPreviews();                // true if any was stopped
        std::optional<services::AnimationEditPreviewStatus> getEditPreviewStatus(entt::entity entity) const;

        void initializeEntityAnimator(entt::entity entity, const std::string& animatorPath);
        void destroyEntityAnimator(entt::entity entity);
        bool hasAnimator(entt::entity entity) const;

        AnimationLayerStack* getLayerStack(entt::entity entity);
        const AnimationLayerStack* getLayerStack(entt::entity entity) const;

        AnimatorStateMachine* getAnimator(entt::entity entity);
        const AnimatorStateMachine* getAnimator(entt::entity entity) const;

        void syncWithRegistry();

        void clearAll();

        const resource::SkeletonData* loadSkeleton(const std::string& meshPath);

        // Unified socket accessor (VK-1427): skeletal sockets for a skinned mesh, or
        // static SOK2 sockets for a non-skeletal mesh. nullptr if the mesh has neither.
        const std::vector<animator::SocketDefinition>* loadSockets(const std::string& meshPath);

        uint32_t getTotalAnimatorCount() const { return static_cast<uint32_t>(animators.size()); }
        AnimationLODManager& getLODManager() { return lodManager; }
        const AnimationLODManager& getLODManager() const { return lodManager; }

        uint32_t getActiveInstanceGroupCount() const { return activeInstanceGroupCount; }

        void setMaxStreamingInitPerFrame(uint32_t count) { maxInitPerFrame = count; }
        uint32_t getMaxStreamingInitPerFrame() const { return maxInitPerFrame; }
        uint32_t getPendingInitCount() const { std::lock_guard<std::mutex> lock(pendingInitMutex); return static_cast<uint32_t>(pendingInitQueue.size()); }

        const std::vector<glm::mat4>* getCachedSocketTransforms(entt::entity entity) const;

    private:
        using ActiveAnimatorList = std::vector<std::pair<entt::entity, AnimationLayerStack*>>;

        RuntimeAnimatorSystem() = default;
        ~RuntimeAnimatorSystem() = default;
        RuntimeAnimatorSystem(const RuntimeAnimatorSystem&) = delete;
        RuntimeAnimatorSystem& operator=(const RuntimeAnimatorSystem&) = delete;

        ActiveAnimatorList evaluateAnimations(float deltaTime);
        void processPendingStreamingInits();
        bool isEntityInFrustum(entt::entity entity, entt::registry& registry) const;
        void publishAnimationEvents(entt::entity entity, AnimationLayerStack* anim);
        void applyRootMotion(entt::entity entity, AnimationLayerStack* anim, entt::registry& registry);
        void applyIKPostProcess(entt::entity entity, AnimationLayerStack* anim, entt::registry& registry);

        struct EvalCandidate
        {
            entt::entity entity;
            AnimationLayerStack* anim;
            uint8_t lodLevel;
        };

        void gatherEvalCandidates(std::vector<EvalCandidate>& evalCandidates,
                                   std::vector<std::pair<entt::entity, AnimationLayerStack*>>& lodInterpolateEntities);
        void classifyInstanceGroups(std::vector<EvalCandidate>& evalCandidates,
                                     ActiveAnimatorList& leadersToEvaluate,
                                     std::vector<std::pair<entt::entity, AnimationLayerStack*>>& followersToSync);
        void evaluateLeadersAndSync(ActiveAnimatorList& leadersToEvaluate,
                                     std::vector<std::pair<entt::entity, AnimationLayerStack*>>& followersToSync,
                                     ActiveAnimatorList& activeAnimators, float deltaTime);
        void cachePosesAndInterpolate(ActiveAnimatorList& activeAnimators,
                                       std::vector<std::pair<entt::entity, AnimationLayerStack*>>& lodInterpolateEntities);

        std::optional<events::animation::snapshot::AnimationSnapshot> captureSnapshot(entt::entity entity);
        void restoreSnapshot(entt::entity entity, const events::animation::snapshot::AnimationSnapshot& snapshot);

        void subscribeToEvents();
        void subscribeToWorldEvents();
        const resource::SkeletonData* resolveEntitySkeleton(entt::entity entity, std::string& outMeshPath);
        void setupEntityAnimatorComponent(entt::entity entity, AnimationLayerStack* layerStack, const std::string& animatorPath);
        // Builds the retarget context for an entity from its mesh's retargetRef. nullptr when
        // there is no (valid) retarget binding. Used for animators and edit preview sessions.
        std::unique_ptr<RetargetContext> makeRetargetContext(entt::entity entity, const resource::SkeletonData* targetSkeleton);

        void recordAnimatorSources(entt::entity entity, const std::string& animatorPath);
        bool animatorSourcesMatch(entt::entity entity, const components::MeshComponent& meshComp) const;
        // Destroys the entity's animator when its sources changed, then (re)initializes it.
        void syncEntityAnimator(entt::entity entity, const components::MeshComponent& meshComp);

        // Applies the queued requests (main thread). Animators are destroyed before the cache
        // entries they point into are dropped; the next sync re-initializes them from disk.
        void applyPendingRequests();
        void reloadAnimator(const std::string& path);
        void reloadMeshes(const std::vector<std::string>& meshPaths);

        void pruneEditPreviews();
        void releasePreviewPose(entt::entity entity);

        uint64_t computeInstanceGroupKey(const std::string& animatorPath, uint32_t stateId,
                                          uint8_t lodLevel, float normalizedTime) const;
    };
#pragma warning(pop)
}
