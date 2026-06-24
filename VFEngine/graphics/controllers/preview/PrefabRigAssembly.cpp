#include "PrefabRigAssembly.hpp"

#include "../../animation/IKPostProcess.hpp"
#include "print/Log.hpp"
#include "asset/AssetRef.hpp"
#include "resource/ResourceManager.hpp"
#include "retargeting/RetargetTypes.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>

namespace controllers
{
    // MUST match render::mesh::MAX_BONES (SkinnedMeshTypes.hpp:10). Defined locally so this
    // CPU-testable TU does not pull in the Vulkan-laden SkinnedMeshTypes.hpp.
    static constexpr size_t MAX_BONES_PER_PART = 128;

    // ----------------------------------------------------------------------
    // prefabrig — pure, CPU-testable math (mirrors the verified Play formulas).
    // ----------------------------------------------------------------------
    namespace prefabrig
    {
        glm::mat4 socketModelTransform(const std::vector<glm::mat4>& boneMatrices,
                                       const std::vector<glm::mat4>& bindPoses,
                                       const animator::SocketDefinition& socket)
        {
            // Mirror AnimationLayerStackControls.cpp:211-227 exactly.
            if (socket.boneIndex >= 0 &&
                socket.boneIndex < static_cast<int32_t>(boneMatrices.size()) &&
                socket.boneIndex < static_cast<int32_t>(bindPoses.size()))
            {
                const glm::vec3 boneMeshPos = glm::vec3(
                    boneMatrices[socket.boneIndex]
                    * bindPoses[socket.boneIndex]
                    * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

                return glm::translate(glm::mat4(1.0f), boneMeshPos + socket.localPosition)
                       * glm::mat4_cast(socket.localRotation);
            }
            return glm::mat4(1.0f);
        }

        glm::mat4 composeChildWorld(const glm::mat4& socketWorld,
                                    const glm::vec3& childRotationDeg,
                                    const glm::vec3& childScale)
        {
            // Mirror SocketAttachmentUpdater::applyModelOffset (SocketAttachmentUpdater.cpp:310-318):
            // entityLocal carries rotation + scale only; the socket's translation is the sole
            // positional contribution (the child's own translation is dropped).
            const glm::mat4 rot = glm::mat4_cast(glm::quat(glm::radians(childRotationDeg)));
            const glm::mat4 entityLocal = rot * glm::scale(glm::mat4(1.0f), childScale);
            return socketWorld * entityLocal;
        }

        glm::vec3 ikTargetFromSocket(const glm::mat4& targetPartWorld,
                                     const animator::SocketDefinition& gripSocket)
        {
            // gripWorld = targetPartWorld * grip.getLocalOffsetMatrix(); the solver target
            // POSITION is that matrix's translation column.
            const glm::mat4 gripWorld = targetPartWorld * gripSocket.getLocalOffsetMatrix();
            return glm::vec3(gripWorld[3]);
        }
    }

    // ----------------------------------------------------------------------
    // PrefabRigAssembly
    // ----------------------------------------------------------------------

    PrefabRigAssembly::PrefabRigAssembly() = default;

    PrefabRigAssembly::~PrefabRigAssembly()
    {
        // Destroy the AnimationLayerStacks (which hold &skeleton / animatorData / cache
        // pointers) BEFORE the per-part skeletons and the dataCache go away. Explicit clear()
        // guarantees this regardless of member-declaration teardown order.
        clear();
    }

    void PrefabRigAssembly::clear()
    {
        // Reset stacks first (they reference skeleton + cache).
        for (auto& part : parts)
            part.stack.reset();
        parts.clear();
        chainConfigs.clear();
        chains.clear();
        topoOrder.clear();
        built = false;
    }

    bool PrefabRigAssembly::build(const PrefabRigDesc& desc)
    {
        clear();

        parts.resize(desc.parts.size());

        // Pass 1: load per-part data and (for skeletal parts) build the layer stacks.
        for (size_t i = 0; i < desc.parts.size(); ++i)
        {
            const PrefabRigPart& src = desc.parts[i];
            Part& part = parts[i];

            part.meshPath = src.meshPath;
            part.animatorPath = src.animatorPath;
            part.parentIndex = src.parentPartIndex;
            part.attachRotationDeg = src.attachChildRotation;
            part.attachScale = src.attachChildScale;
            part.localTransform = src.localTransform;

            // Skeleton (by value): a copy of the cache entry so it outlives the stack and so
            // editable sockets are independent of the shared cache copy.
            const resource::SkeletonData* skel = dataCache.loadSkeleton(src.meshPath);
            if (skel)
                part.skeleton = *skel; // deep copy

            // Editable socket authoring copy: skeletal sockets live on the skeleton; static
            // meshes carry a SOK2 block. loadSockets unifies both.
            if (const std::vector<animator::SocketDefinition>* socks = dataCache.loadSockets(src.meshPath))
                part.sockets = *socks;

            const bool wantsAnimator = !src.animatorPath.empty();
            const bool hasSkeleton = !part.skeleton.bones.empty();

            if (wantsAnimator && hasSkeleton)
            {
                if (part.skeleton.bones.size() > MAX_BONES_PER_PART)
                {
                    vfLogWarning("[PrefabRigAssembly] Part {} mesh '{}' has {} bones, exceeds MAX_BONES {} "
                                 "- skinning will be clamped",
                                 i, src.meshPath, part.skeleton.bones.size(), MAX_BONES_PER_PART);
                }

                part.animatorData = dataCache.loadAnimatorData(src.animatorPath);
                if (part.animatorData)
                {
                    // Optional retarget: mirror RuntimeAnimatorSystem::buildEntityRetargetContext
                    // (RuntimeAnimatorSystemInit.cpp:15-65) but driven by the desc's retargetPath
                    // (the .vfretarget map) instead of MeshComponent.retargetRef.
                    if (!src.retargetPath.empty())
                    {
                        if (auto retarget = buildRetargetForPart(part, src.retargetPath))
                            part.retarget = std::move(retarget);
                    }

                    part.stack = std::make_unique<animation::AnimationLayerStack>();
                    part.stack->initialize(*part.animatorData, &part.skeleton,
                        [this](const std::string& path) -> const resource::AnimationData*
                        {
                            return dataCache.loadAnimation(path);
                        },
                        part.retarget.get());

                    part.skeletal = part.stack->isInitialized();
                    if (!part.skeletal)
                    {
                        vfLogWarning("[PrefabRigAssembly] Part {} animator '{}' failed to initialize",
                                     i, src.animatorPath);
                        part.stack.reset();
                    }
                }
                else
                {
                    vfLogWarning("[PrefabRigAssembly] Part {} failed to load animator '{}'",
                                 i, src.animatorPath);
                }
            }
            else if (wantsAnimator && !hasSkeleton)
            {
                vfLogWarning("[PrefabRigAssembly] Part {} mesh '{}' has no skeleton; animator '{}' ignored",
                             i, src.meshPath, src.animatorPath);
            }

            // Static parts feed an identity bone set to the skinned pipeline (Layer B).
            if (!part.skeletal)
                part.identityBones.assign(1, glm::mat4(1.0f));
        }

        // Resolve attachment socket indices on the parent part (by name).
        for (size_t i = 0; i < desc.parts.size(); ++i)
        {
            const PrefabRigPart& src = desc.parts[i];
            Part& part = parts[i];
            if (part.parentIndex >= 0 && part.parentIndex < static_cast<int>(parts.size()) &&
                !src.attachParentSocket.empty())
            {
                const Part& parent = parts[part.parentIndex];
                part.parentSocketIndex = animator::indexOfSocket(parent.sockets, src.attachParentSocket);
                if (part.parentSocketIndex < 0)
                {
                    vfLogWarning("[PrefabRigAssembly] Part {} attach socket '{}' not found on parent part {}",
                                 i, src.attachParentSocket, part.parentIndex);
                }
            }
        }

        // IK chains: copy configs (editable) and resolve their part/socket bindings.
        chainConfigs.reserve(desc.ik.size());
        chains.reserve(desc.ik.size());
        for (const PrefabRigIK& srcIk : desc.ik)
        {
            chainConfigs.push_back(srcIk.chain);

            Chain c;
            c.bodyPartIndex = srcIk.bodyPartIndex;
            c.targetPartIndex = srcIk.targetPartIndex;
            if (srcIk.targetPartIndex >= 0 && srcIk.targetPartIndex < static_cast<int>(parts.size()) &&
                !srcIk.targetSocketName.empty())
            {
                c.targetSocketIndex = animator::indexOfSocket(parts[srcIk.targetPartIndex].sockets,
                                                              srcIk.targetSocketName);
                if (c.targetSocketIndex < 0)
                {
                    vfLogWarning("[PrefabRigAssembly] IK chain '{}' target socket '{}' not found on part {}",
                                 srcIk.chain.chainName, srcIk.targetSocketName, srcIk.targetPartIndex);
                }
            }
            chains.push_back(std::move(c));
        }

        resolveTopologicalOrder();

        // Built if anything loaded; a static-only desc is still a valid (renderable) build.
        built = !parts.empty();
        if (!built)
            vfLogWarning("[PrefabRigAssembly] build produced no parts");
        return built;
    }

    std::unique_ptr<animation::RetargetContext> PrefabRigAssembly::buildRetargetForPart(
        const Part& part, const std::string& retargetPath)
    {
        // Mirror RuntimeAnimatorSystem::buildEntityRetargetContext (RuntimeAnimatorSystemInit.cpp:30-64).
        auto map = resource::ResourceManager::loadRetargetMap(asset::AssetRef::fromPath(retargetPath));
        if (!map)
        {
            vfLogWarning("[PrefabRigAssembly] Retarget map '{}' failed to load", retargetPath);
            return nullptr;
        }

        auto sourceRig = resource::ResourceManager::loadHumanoidRig(
            asset::AssetRef::fromHexString(map->sourceRigAssetGuid));
        auto targetRig = resource::ResourceManager::loadHumanoidRig(
            asset::AssetRef::fromHexString(map->targetRigAssetGuid));
        if (!sourceRig || !targetRig)
        {
            vfLogWarning("[PrefabRigAssembly] Retarget rig(s) for '{}' failed to load", retargetPath);
            return nullptr;
        }

        // Source skeleton is optional (context degrades to ratio 1 / unscaled hips).
        const resource::SkeletonData* sourceSkeleton = nullptr;
        resource::SkeletonData sourceSkeletonStorage;
        if (!sourceRig->sourceSkeletonAssetGuid.empty())
        {
            const std::string sourceMeshPath =
                asset::AssetRef::fromHexString(sourceRig->sourceSkeletonAssetGuid).resolve();
            if (!sourceMeshPath.empty())
            {
                if (const resource::SkeletonData* s = dataCache.loadSkeleton(sourceMeshPath))
                {
                    sourceSkeletonStorage = *s; // RetargetContext::build only reads it during build
                    sourceSkeleton = &sourceSkeletonStorage;
                }
            }
        }

        auto ctx = std::make_unique<animation::RetargetContext>(
            animation::RetargetContext::build(part.skeleton, sourceSkeleton, *sourceRig, *targetRig, *map));
        if (ctx->empty())
        {
            vfLogWarning("[PrefabRigAssembly] Retarget context for '{}' is empty (no role overlap)", retargetPath);
            return nullptr;
        }
        return ctx;
    }

    void PrefabRigAssembly::resolveTopologicalOrder()
    {
        // Parent-before-child ordering so a depth-N attachment chain converges in ONE update()
        // (mirrors SocketAttachmentUpdater::resolveChain's parent-first apply, VK-1432).
        topoOrder.clear();
        topoOrder.reserve(parts.size());

        std::vector<bool> placed(parts.size(), false);

        // Repeatedly place any part whose parent is already placed (or has no parent).
        // Cycles / dangling parents are placed at the end so nothing is lost.
        bool progress = true;
        while (progress)
        {
            progress = false;
            for (size_t i = 0; i < parts.size(); ++i)
            {
                if (placed[i])
                    continue;
                const int p = parts[i].parentIndex;
                const bool parentReady =
                    p < 0 || p >= static_cast<int>(parts.size()) || placed[static_cast<size_t>(p)];
                if (parentReady)
                {
                    topoOrder.push_back(i);
                    placed[i] = true;
                    progress = true;
                }
            }
        }
        // Any remaining (cyclic) parts: append in index order to stay total.
        for (size_t i = 0; i < parts.size(); ++i)
            if (!placed[i])
                topoOrder.push_back(i);
    }

    void PrefabRigAssembly::update(float dt)
    {
        if (!built)
            return;

        // --- 1. advance each skeletal part's animator (or hold its rest pose) -------------
        for (Part& part : parts)
        {
            if (!part.stack)
                continue;
            if (paused)
                part.stack->evaluateRestPose();
            else
                part.stack->update(dt);
        }

        // --- 2-4. resolve attachments + feed/apply IK (shared with the scrub paths) -------
        resolveAttachmentsAndIK();
    }

    void PrefabRigAssembly::resolveAttachmentsAndIK()
    {
        if (!built)
            return;

        // --- 2. resolve the attachment chain (topological, parent-before-child) ----------
        for (size_t idx : topoOrder)
        {
            Part& part = parts[idx];

            if (part.parentIndex < 0 || part.parentIndex >= static_cast<int>(parts.size()))
            {
                // Root part: world = preview/turntable model matrix, then the editor-transient
                // gizmo transform (so a root gizmo edit moves the WHOLE rig, descendants included),
                // then the prefab's authored local transform (rotation/scale/position) so the
                // preview matches the scene viewport.
                part.partWorld = rootModelMatrix * part.previewTransform * part.localTransform;
                continue;
            }

            Part& parent = parts[static_cast<size_t>(part.parentIndex)];

            glm::mat4 parentSocketModel(1.0f);
            if (part.parentSocketIndex >= 0 &&
                part.parentSocketIndex < static_cast<int>(parent.sockets.size()))
            {
                const animator::SocketDefinition& socket = parent.sockets[part.parentSocketIndex];
                if (parent.stack)
                {
                    // Skeletal parent: bone-socket model transform from the live pose.
                    parentSocketModel = prefabrig::socketModelTransform(
                        parent.stack->getBoneMatrices(), parent.skeleton.bindPoses, socket);
                }
                else
                {
                    // Static parent: the local offset matrix IS the model-space transform
                    // (SocketAttachmentUpdater.cpp:285-287).
                    parentSocketModel = socket.getLocalOffsetMatrix();
                }
            }

            const glm::mat4 socketWorld = parent.partWorld * parentSocketModel;
            // Attachment compose, then the part's editor-transient gizmo transform in its own local
            // space (descendants compose off this partWorld, so they follow the gizmo edit too).
            part.partWorld = prefabrig::composeChildWorld(socketWorld, part.attachRotationDeg, part.attachScale)
                             * part.previewTransform;
        }

        // --- 3. feed each IK chain's runtime target from its bound grip socket ------------
        for (size_t i = 0; i < chains.size(); ++i)
        {
            Chain& c = chains[i];
            components::IKChainRuntimeState& state = c.runtimeState;

            const bool boundTarget =
                c.targetPartIndex >= 0 && c.targetPartIndex < static_cast<int>(parts.size()) &&
                c.targetSocketIndex >= 0 &&
                c.targetSocketIndex < static_cast<int>(parts[c.targetPartIndex].sockets.size());

            if (!boundTarget)
            {
                state.isActive = false;
                state.currentWeight = 0.0f;
                continue;
            }

            const Part& targetPart = parts[static_cast<size_t>(c.targetPartIndex)];
            const animator::SocketDefinition& grip = targetPart.sockets[c.targetSocketIndex];

            state.targetPosition = prefabrig::ikTargetFromSocket(targetPart.partWorld, grip);
            state.targetRotation = std::nullopt; // grip orientation not driven (matches script feed)
            state.isActive = true;
            state.currentWeight = (i < chainConfigs.size()) ? chainConfigs[i].weight : 1.0f;
        }

        // --- 4. apply IK on each body part that owns chains -------------------------------
        for (size_t bodyIdx = 0; bodyIdx < parts.size(); ++bodyIdx)
        {
            Part& body = parts[bodyIdx];
            if (!body.stack)
                continue;

            // Gather the chains (and their runtime states) owned by this body part. applyIK
            // expects parallel chain/runtimeState vectors, so build per-body views.
            std::vector<animator::ik::IKChainConfig> bodyChains;
            std::vector<components::IKChainRuntimeState> bodyStates;
            std::vector<size_t> bodyChainIndices;
            for (size_t i = 0; i < chains.size(); ++i)
            {
                if (chains[i].bodyPartIndex == static_cast<int>(bodyIdx))
                {
                    bodyChains.push_back(chainConfigs[i]);
                    bodyStates.push_back(chains[i].runtimeState);
                    bodyChainIndices.push_back(i);
                }
            }
            if (bodyChains.empty())
                continue;

            // entityWorldMatrix = the body part's world matrix (matches RuntimeAnimatorSystem.cpp:257).
            animation::IKPostProcessor::applyIK(body.stack->getMutableBoneMatrices(), body.skeleton,
                                                bodyChains, bodyStates, body.partWorld);

            // Write back resolved indices / runtime state so the lazy bone-name resolve persists.
            for (size_t k = 0; k < bodyChainIndices.size(); ++k)
                chains[bodyChainIndices[k]].runtimeState = bodyStates[k];
        }
    }

    // ----------------------------------------------------------------------
    // Accessors
    // ----------------------------------------------------------------------

    const std::vector<glm::mat4>& PrefabRigAssembly::boneMatrices(size_t part) const
    {
        if (part >= parts.size())
            return emptyMatrices;
        const Part& p = parts[part];
        if (p.stack)
            return p.stack->getBoneMatrices();
        return p.identityBones;
    }

    glm::mat4 PrefabRigAssembly::partWorld(size_t part) const
    {
        if (part >= parts.size())
            return glm::mat4(1.0f);
        return parts[part].partWorld;
    }

    const std::string& PrefabRigAssembly::meshPath(size_t part) const
    {
        static const std::string empty;
        if (part >= parts.size())
            return empty;
        return parts[part].meshPath;
    }

    bool PrefabRigAssembly::isSkeletalPart(size_t part) const
    {
        return part < parts.size() && parts[part].skeletal;
    }

    std::vector<animator::SocketDefinition>& PrefabRigAssembly::editableSockets(size_t part)
    {
        if (part >= parts.size())
            return emptySockets;
        return parts[part].sockets;
    }

    const std::vector<animator::SocketDefinition>& PrefabRigAssembly::editableSockets(size_t part) const
    {
        if (part >= parts.size())
            return emptySockets;
        return parts[part].sockets;
    }

    std::vector<animator::ik::IKChainConfig>& PrefabRigAssembly::editableChains()
    {
        return chainConfigs;
    }

    const std::vector<animator::ik::IKChainConfig>& PrefabRigAssembly::editableChains() const
    {
        return chainConfigs;
    }

    bool PrefabRigAssembly::forceState(size_t part, const std::string& stateName, float blendDuration)
    {
        if (part >= parts.size() || !parts[part].stack)
            return false;
        return parts[part].stack->forceTransitionTo(stateName, blendDuration);
    }

    const animator::AnimatorData* PrefabRigAssembly::animatorData(size_t part) const
    {
        if (part >= parts.size() || !parts[part].stack)
            return nullptr;
        return parts[part].stack->getAnimatorData();
    }

    void PrefabRigAssembly::setBool(size_t part, const std::string& name, bool value)
    {
        if (part < parts.size() && parts[part].stack)
            parts[part].stack->setBool(name, value);
    }

    void PrefabRigAssembly::setFloat(size_t part, const std::string& name, float value)
    {
        if (part < parts.size() && parts[part].stack)
            parts[part].stack->setFloat(name, value);
    }

    void PrefabRigAssembly::setInt(size_t part, const std::string& name, int32_t value)
    {
        if (part < parts.size() && parts[part].stack)
            parts[part].stack->setInt(name, value);
    }

    void PrefabRigAssembly::setTrigger(size_t part, const std::string& name)
    {
        if (part < parts.size() && parts[part].stack)
            parts[part].stack->setTrigger(name);
    }

    void PrefabRigAssembly::play()
    {
        // The `paused` flag alone selects update() vs evaluateRestPose() (the VK-1407
        // edit-mode rest-pose behavior). We do not toggle the stack's internal play/pause
        // state so the active state machine resumes from where it left off on Play.
        paused = false;
    }

    void PrefabRigAssembly::pause()
    {
        paused = true;
    }

    // ----------------------------------------------------------------------
    // Frame-by-frame scrub (VK-1433)
    // ----------------------------------------------------------------------

    void PrefabRigAssembly::stepFrame(size_t part, int frames)
    {
        if (part >= parts.size() || !parts[part].stack || frames == 0)
            return;

        animation::AnimationLayerStack* stack = parts[part].stack.get();
        const float frameDt = stack->getCurrentClipFrameDuration(); // seconds per source frame

        if (frames > 0)
        {
            // Forward: advance the animator by frames * frameDt seconds. The stack's internal state
            // machine stays "playing" (pause() only flips our `paused` gate, not the stack), so this
            // is exactly the Play update — just applied while the rig preview is paused, firing the
            // same transitions/events a real step would.
            stack->update(static_cast<float>(frames) * frameDt);
        }
        else
        {
            // Backward: update() cannot take a negative dt, so seek. Convert the negative seconds
            // step to a normalized delta via the current state duration and re-seek (clamped to 0).
            const float durationSec = stack->getCurrentStateDuration();
            const float current = stack->getNormalizedTime();
            const float normDelta = (durationSec > 0.0f)
                                        ? (static_cast<float>(frames) * frameDt / durationSec)
                                        : 0.0f;
            stack->setNormalizedTime(std::max(0.0f, current + normDelta));
        }

        // Re-resolve so attached parts and IK follow the new pose immediately.
        resolveAttachmentsAndIK();
    }

    void PrefabRigAssembly::setNormalizedTime(size_t part, float t)
    {
        if (part >= parts.size() || !parts[part].stack)
            return;

        parts[part].stack->setNormalizedTime(t);
        resolveAttachmentsAndIK();
    }

    float PrefabRigAssembly::normalizedTime(size_t part) const
    {
        if (part >= parts.size() || !parts[part].stack)
            return 0.0f;
        return parts[part].stack->getNormalizedTime();
    }

    // ----------------------------------------------------------------------
    // Preview transform (editor-transient)
    // ----------------------------------------------------------------------

    void PrefabRigAssembly::setPartPreviewTransform(size_t part, const glm::mat4& m)
    {
        if (part < parts.size())
            parts[part].previewTransform = m;
    }

    const glm::mat4& PrefabRigAssembly::partPreviewTransform(size_t part) const
    {
        static const glm::mat4 identity(1.0f);
        if (part >= parts.size())
            return identity;
        return parts[part].previewTransform;
    }

    void PrefabRigAssembly::resetPreviewTransforms()
    {
        for (Part& part : parts)
            part.previewTransform = glm::mat4(1.0f);
    }
}
