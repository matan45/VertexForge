#pragma once

// VK-1433 Layer B — Prefab Rig Preview, multi-mesh offscreen controller.
//
// Renders a PrefabRigAssembly (a full assembled character rig: skeletal body + socket-
// attached weapon(s) + IK, all entt-free) to ONE ImGui image. It generalizes
// AnimatedMeshPreviewController (single skinned mesh) to N parts sharing ONE
// OffscreenResources: every renderable part owns its own SkinnedMeshPipeline (each with its
// own camera UBO / bone SSBO / material textures), and render() records them sequentially
// with load semantics so the parts composite into a single color+depth target.
//
// Material fidelity (user-chosen): each part renders with its assigned material's real PBR
// textures (albedo/normal/ORM-or-MRA/emission) extracted via MaterialPBRExtractor, lit by
// the default IBL. Static parts render through the SAME material-capable SkinnedMeshPipeline
// with an identity bone set — a no-skin .vfMesh carries zeroed bone weights, so the skinned
// vertex shader passes its vertices through unskinned (verified: resource::Vertex always
// carries boneIndices{-1}/boneWeights{0}).
//
// The Editor never includes this controller directly — Layer C reaches it through the
// preview provider/adapter/service boundary (a PrefabRigDescDTO crosses, this stays in
// Graphics).

#include <glm/glm.hpp>
#include "../../../services/providers/render/IMeshPreviewProvider.hpp"
#include "../../../services/providers/render/IPrefabRigPreviewProvider.hpp" // services::PrefabRigJoint
#include "../../render/mesh/SkinnedMeshTypes.hpp" // pulls <vulkan/vulkan.hpp> (vk::Sampler/Fence)
#include "../../render/tools/ImmediateDebugTypes.hpp"
#include "PrefabRigAssembly.hpp"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
    class CommandPool;
    struct OffscreenResources;
}

namespace render
{
    class ClearColor;
}

namespace render::preview
{
    class PreviewBackgroundRenderer;
    class PreviewGridRenderer;
    class PreviewSkeletonOverlayRenderer;
}

namespace render::mesh
{
    class SkinnedMeshPipeline;
}

namespace controllers
{
    class PrefabRigPreviewController
    {
    public:
        explicit PrefabRigPreviewController();
        ~PrefabRigPreviewController();

        PrefabRigPreviewController(const PrefabRigPreviewController&) = delete;
        PrefabRigPreviewController& operator=(const PrefabRigPreviewController&) = delete;
        PrefabRigPreviewController(PrefabRigPreviewController&&) = delete;
        PrefabRigPreviewController& operator=(PrefabRigPreviewController&&) = delete;

        void init();
        void cleanUp();

        // Builds the assembly from the description, then stands up one SkinnedMeshPipeline per
        // part: loads its mesh geometry and (if a material is referenced on the part) binds the
        // material's real PBR textures. Re-callable: tears the previous build down first.
        // Returns false (and logs) if the assembly produced no parts.
        bool buildFromDesc(const PrefabRigDesc& desc);

        // Cheap transform-only update: forwards a structure-identical desc's transform fields to the
        // assembly WITHOUT any waitIdle / pipeline teardown / mesh reload. Returns false if the
        // structure drifted (or nothing is built) so the caller can fall back to buildFromDesc().
        bool updateTransformsFromDesc(const PrefabRigDesc& desc);

        // Advances the assembly (animators -> attachments -> IK), then re-uploads each skeletal
        // part's composed bone matrices to its pipeline.
        void update(float deltaTime);

        void updateCamera(const glm::mat4& view, const glm::mat4& projection,
                          const glm::vec3& cameraPos);

        void setEnvironment(const services::PreviewEnvironmentParams& params) { environmentParams = params; }

        // Root (turntable) model matrix applied to root parts; propagates through attachments.
        void setRootModelMatrix(const glm::mat4& m);

        // --- State / authoring pass-throughs (delegate to the owned assembly) -------------
        bool forceState(size_t part, const std::string& stateName, float blendDuration = 0.25f);
        const animator::AnimatorData* animatorData(size_t part) const;
        void setBool(size_t part, const std::string& name, bool value);
        void setFloat(size_t part, const std::string& name, float value);
        void setInt(size_t part, const std::string& name, int32_t value);
        void setTrigger(size_t part, const std::string& name);
        void play();
        void pause();
        bool isPaused() const;

        // Frame-by-frame scrub (VK-1433): step / seek one skeletal part, even while paused.
        void stepFrame(size_t part, int frames);
        void setNormalizedTime(size_t part, float t);
        float normalizedTime(size_t part) const;

        // Editor-transient gizmo transform per part (VK-1433); folded into partWorld each update().
        // Never serialized. partWorld() exposes the LIVE composed world for the gizmo anchor.
        void setPartPreviewTransform(size_t part, const glm::mat4& m);
        const glm::mat4& partPreviewTransform(size_t part) const;
        void resetPreviewTransforms();
        glm::mat4 partWorld(size_t part) const;

        // VK-1433 Phase 1b — world-space joints of a skeletal part for editor bone-picking. Built
        // from the live assembly with the SAME formula the skeleton overlay draws; one entry per
        // valid bone. Empty for static / out-of-range parts. Pure CPU read-back (no GPU work).
        std::vector<services::PrefabRigJoint> jointWorlds(size_t part) const;

        std::vector<animator::SocketDefinition>& editableSockets(size_t part);
        const std::vector<animator::SocketDefinition>& editableSockets(size_t part) const;
        std::vector<animator::ik::IKChainConfig>& editableChains();
        const std::vector<animator::ik::IKChainConfig>& editableChains() const;
        // Re-resolves cached by-name socket bindings after editableSockets() was overwritten
        // (a rename/reorder leaves child-attach / IK-target indices stale); delegates to the assembly.
        void reresolveSocketBindings();

        size_t partCount() const;
        bool isBuilt() const { return built; }

        // Renders all parts to one image; returns the ImGui descriptor set for ImGui::Image().
        void* render();

    private:
        void createOffscreenResources();
        void cleanupOffscreenResources();
        void createSampler();
        void updateDescriptorSet(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const;

        // Builds one pipeline for a renderable part: loads geometry + (optional) material.
        bool buildPartPipeline(size_t partIndex, const PrefabRigPart& descPart);

        void destroyPipelines();

        // Rebuilds `overlayLines` from the live assembly when any overlay toggle is on. Pure CPU
        // (no GPU work); the resulting draw list is uploaded + drawn as render() "Step 5".
        void buildOverlayLines();

        // Orders one part's color+depth attachment writes before the next part's load. Separate
        // dynamic-rendering instances writing the same attachment have NO implicit dependency,
        // so this barrier is required between consecutive part draws (color/depth stay in their
        // attachment-optimal layouts; this is a memory/execution barrier, not a layout change).
        void barrierBetweenParts(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

        core::Device& device;
        core::SwapChain& swapChain;
        std::unique_ptr<core::CommandPool> commandPool;

        vk::Sampler sampler;
        std::unique_ptr<core::OffscreenResources> offscreenResources;
        std::vector<vk::Fence> inFlightFences;

        std::unique_ptr<render::ClearColor> clearColor;
        std::unique_ptr<render::preview::PreviewBackgroundRenderer> previewBackground;
        std::unique_ptr<render::preview::PreviewGridRenderer> previewGrid;
        std::unique_ptr<render::preview::PreviewSkeletonOverlayRenderer> skeletonOverlay;
        services::PreviewEnvironmentParams environmentParams;

        // Rebuilt each frame in render() from the live assembly (Phase 1 debug overlay).
        render::mesh::ImmediateDebugDrawList overlayLines;

        PrefabRigAssembly assembly;

        // One pipeline per part. A null entry means "part not renderable" (e.g. mesh failed to
        // load); render() skips nulls. Index is parallel to assembly part indices.
        std::vector<std::unique_ptr<render::mesh::SkinnedMeshPipeline>> pipelines;

        // Per-part cached material render data: the extracted scalar PBR (used by the shader
        // for any empty texture slot) plus the packed texture indices (which slots are bound).
        // Parallel to `pipelines`; index i is the material of part i. Default-constructed
        // entries (no material) carry the neutral scalar defaults + all-NONE indices, matching
        // the AnimatedMeshPreviewController look.
        std::vector<render::mesh::SkinnedMeshRenderData> partMaterial;

        glm::mat4 currentView{1.0f};
        glm::mat4 currentProjection{1.0f};
        glm::vec3 currentCameraPos{0.0f};

        bool initialized = false;
        bool built = false;

        // Idempotency guard for cleanUp()/the destructor. Starts true (no live GPU
        // resources to release), flipped to false the moment init() begins creating
        // device resources, and flipped back to true once cleanUp() has torn them down.
        // Lets cleanUp()/~PrefabRigPreviewController run safely twice — or after the
        // Device has been destroyed — without re-issuing vkDeviceWaitIdle on dead state.
        bool previewCleanedUp = true;
    };
}
