#include "print/Log.hpp"
#include "VFXSequenceRuntimeServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/vfx/VFXRuntimeEvents.hpp"
#include "../../events/vfx/VFXSequenceRuntimeEvents.hpp"
#include "../../events/vfx/VFXEventNotifications.hpp"
#include "../../events/physics/SocketEvents.hpp"
#include "../../events/project/ResourceEvents.hpp"
#include "../../events/audio/AudioEvents.hpp" // VK-1496 — Sound-step fan-out (dispatched here only)
#include "../../data/VFXOverrideApplier.hpp"
#include "asset/AssetDatabase.hpp"
#include "vfx/VFXSequenceAsset.hpp"
#include "vfx/VFXAsset.hpp"
#include "vfx/VFXBoundsUtil.hpp"
#include "vfx/VFXRuntimeDiagnostics.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace services
{
    namespace
    {
        // Minimal RAII scope-exit: runs `fn` when the scope is left by ANY path (normal
        // return or an exception unwinding through it). Used to balance comboTeardownGuard so
        // a throwing dispatch can never leave the guard stuck >0 (review #11). No generic
        // scope-guard exists in the codebase; this is deliberately file-local and tiny.
        template <typename F>
        struct ScopeExit
        {
            F fn;
            explicit ScopeExit(F&& f) : fn(std::move(f)) {}
            ~ScopeExit() { fn(); }
            ScopeExit(const ScopeExit&) = delete;
            ScopeExit& operator=(const ScopeExit&) = delete;
        };

        std::string normalizeSequenceCachePath(const std::string& path)
        {
            if (path.empty())
                return {};

            std::string normalized = asset::AssetDatabase::instance().resolveAssetPath(path);
            std::replace(normalized.begin(), normalized.end(), '\\', '/');

            std::filesystem::path p(normalized);
            normalized = p.lexically_normal().string();
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            while (!normalized.empty() && normalized.back() == '/')
                normalized.pop_back();

#ifdef _WIN32
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
            return normalized;
        }

        // VK-1453 (AC6) — route combo warnings through the deduplicating diagnostics
        // collector so a repeating condition only logs once and repeats bump a count
        // (surfaced in the editor VFX debug window via GetVFXRecentWarningsQuery).
        void reportWarning(const std::string& msg)
        {
            if (vfx::VFXRuntimeDiagnostics::instance().report("VFXSequence", msg))
                vfLogWarning("{}", msg);
        }

        // VK-1524 — a source step's trailing OnDeath/OnCollision notification arrives a few frames
        // after its child stops (readbackEvents reads the previous frame's GPU buffer +
        // MAX_FRAMES_IN_FLIGHT-1 latency, plus the ~1-frame render->update marshalling FIFO). Keep
        // a just-reaped source routable for this many active frames so its final event still spawns
        // the bound receiver instead of being dropped.
        constexpr int kSourceGraceFrames = 8;
        // VK-1524 — EveryEvent spawn-storm backstop: at most this many receiver spawns per receiver
        // per drain tick (the per-receiver live `eventBudget` is the primary control).
        constexpr uint32_t kMaxEventSpawnsPerReceiverPerTick = 8;
    }

    VFXSequenceRuntimeServiceImpl::~VFXSequenceRuntimeServiceImpl()
    {
        if (assetSavedToken.isValid())
        {
            ::events::EventDispatcher::instance().unsubscribe(assetSavedToken);
            assetSavedToken = {};
        }
        if (particleEventToken.isValid())
        {
            ::events::EventDispatcher::instance().unsubscribe(particleEventToken);
            particleEventToken = {};
        }
    }

    void VFXSequenceRuntimeServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::vfxsequence::CreateVFXComboInstanceCommand>(
            [this](const events::vfxsequence::CreateVFXComboInstanceCommand& cmd)
            {
                return createCombo(cmd.sequenceAssetPath, cmd.worldTransform, cmd.entityId, cmd.autoDestroyOnFinish,
                                   cmd.seed, cmd.prewarm, cmd.playbackRate, cmd.fixedStep, cmd.loopSequence);
            });

        dispatcher.registerCommandHandler<events::vfxsequence::DestroyVFXComboInstanceCommand>(
            [this](const events::vfxsequence::DestroyVFXComboInstanceCommand& cmd) { destroyCombo(cmd.comboId); });

        dispatcher.registerCommandHandler<events::vfxsequence::PlayVFXComboInstanceCommand>(
            [this](const events::vfxsequence::PlayVFXComboInstanceCommand& cmd) { playCombo(cmd.comboId); });

        dispatcher.registerCommandHandler<events::vfxsequence::StopVFXComboInstanceCommand>(
            [this](const events::vfxsequence::StopVFXComboInstanceCommand& cmd) { stopCombo(cmd.comboId); });

        dispatcher.registerCommandHandler<events::vfxsequence::ResetVFXComboInstanceCommand>(
            [this](const events::vfxsequence::ResetVFXComboInstanceCommand& cmd) { resetCombo(cmd.comboId); });

        dispatcher.registerCommandHandler<events::vfxsequence::SetVFXComboInstanceTransformCommand>(
            [this](const events::vfxsequence::SetVFXComboInstanceTransformCommand& cmd)
            {
                setComboTransform(cmd.comboId, cmd.worldTransform);
            });

        dispatcher.registerCommandHandler<events::vfxsequence::AttachVFXComboInstanceToSocketCommand>(
            [this](const events::vfxsequence::AttachVFXComboInstanceToSocketCommand& cmd)
            {
                attachComboToSocket(cmd.comboId, EntityHandle{cmd.entityHandle}, cmd.socketName);
            });

        dispatcher.registerCommandHandler<events::vfxsequence::DetachVFXComboInstanceCommand>(
            [this](const events::vfxsequence::DetachVFXComboInstanceCommand& cmd) { detachCombo(cmd.comboId); });

        dispatcher.registerCommandHandler<events::vfxsequence::TriggerVFXComboCueCommand>(
            [this](const events::vfxsequence::TriggerVFXComboCueCommand& cmd)
            {
                triggerCue(cmd.comboId, cmd.cueName, cmd.payload);
            });

        dispatcher.registerCommandHandler<events::vfxsequence::UpdateVFXSequenceRuntimeCommand>(
            [this](const events::vfxsequence::UpdateVFXSequenceRuntimeCommand& cmd) { update(cmd.deltaTime); });

        dispatcher.registerCommandHandler<events::vfxsequence::SetVFXComboPausedCommand>(
            [this](const events::vfxsequence::SetVFXComboPausedCommand& cmd) { setComboPaused(cmd.comboId, cmd.paused); });

        dispatcher.registerCommandHandler<events::vfxsequence::SetVFXComboPlaybackRateCommand>(
            [this](const events::vfxsequence::SetVFXComboPlaybackRateCommand& cmd)
            {
                setComboPlaybackRate(cmd.comboId, cmd.rate);
            });

        dispatcher.registerCommandHandler<events::vfxsequence::SeekVFXComboCommand>(
            [this](const events::vfxsequence::SeekVFXComboCommand& cmd) { seekCombo(cmd.comboId, cmd.seconds); });

        dispatcher.registerQueryHandler<events::vfxsequence::IsVFXComboInstancePlayingQuery>(
            [this](const events::vfxsequence::IsVFXComboInstancePlayingQuery& q) { return isComboPlaying(q.comboId); });

        dispatcher.registerQueryHandler<events::vfxsequence::GetVFXComboStatsQuery>(
            [this](const events::vfxsequence::GetVFXComboStatsQuery&)
            {
                events::vfxsequence::VFXComboStatsResult result;
                result.activeCombos = static_cast<uint32_t>(combos.size());
                for (const auto& [id, combo] : combos)
                {
                    bool anyLive = false;
                    for (const auto& step : combo.steps)
                    {
                        if (step.childId != 0)
                        {
                            anyLive = true;
                            ++result.liveChildInstances;
                        }
                    }
                    // VK-1524 — event-children are live instances too.
                    for (const auto& ec : combo.eventChildren)
                    {
                        if (ec.childId != 0)
                        {
                            anyLive = true;
                            ++result.liveChildInstances;
                        }
                    }
                    // "Playing" = still driving output: explicitly playing, a child alive,
                    // or steps still pending their spawn time.
                    if (combo.playing || anyLive || !combo.timeline.allStepsSpawned())
                        ++result.playingCombos;
                }
                result.culledSpawns = culledSpawns;
                result.pooledReuses = pooledReuses;
                return result;
            });

        if (assetSavedToken.isValid())
            dispatcher.unsubscribe(assetSavedToken);
        assetSavedToken = dispatcher.subscribe<::events::resource::AssetSavedNotification>(
            [this](const ::events::resource::AssetSavedNotification& n)
            {
                // VK-1460: this fires on the publishing (editor) thread. Do NOT touch the
                // cache maps here — just queue the path and let update() invalidate on the
                // update thread, where spawnStep reads them (see pendingSequenceInvalidations).
                std::lock_guard<std::mutex> lock(sequenceInvalidationMutex);
                pendingSequenceInvalidations.push_back(n.filePath);
            });

        // VK-1524 — particle step-output events. Published on the render thread from GPU readback;
        // the callback only mirrors the fields into the FIFO under `particleEventMutex` (it must
        // not touch `combos`). update() drains and routes them on the update thread (1-frame
        // latency). Editor scrub is structurally event-free (the CPU preview emits none).
        if (particleEventToken.isValid())
            dispatcher.unsubscribe(particleEventToken);
        particleEventToken = dispatcher.subscribe<events::vfxruntime::VFXParticleEventNotification>(
            [this](const events::vfxruntime::VFXParticleEventNotification& n)
            {
                std::lock_guard<std::mutex> lock(particleEventMutex);
                pendingParticleEvents.push_back(
                    PendingParticleEvent{n.eventType, n.position, n.velocity, n.color, n.size,
                                         n.parentInstanceId});
            });
    }

    std::shared_ptr<const vfx::VFXSequenceData> VFXSequenceRuntimeServiceImpl::loadSequence(const std::string& path)
    {
        if (path.empty())
            return nullptr;

        auto it = sequenceCache.find(path);
        if (it != sequenceCache.end())
            return it->second;

        auto loaded = vfx::VFXSequenceAsset::load(path);
        if (!loaded)
        {
            sequenceCache[path] = nullptr; // negative-cache so we warn only once per path
            return nullptr;
        }
        auto shared = std::make_shared<const vfx::VFXSequenceData>(std::move(*loaded));
        sequenceCache[path] = shared;
        return shared;
    }

    const VFXSequenceRuntimeServiceImpl::ChildVFXInfo&
    VFXSequenceRuntimeServiceImpl::loadChildInfo(const std::string& path)
    {
        auto it = childCache.find(path);
        if (it != childCache.end())
            return it->second;

        ChildVFXInfo info;
        if (auto data = vfx::VFXAsset::load(path))
        {
            info.valid = true;
            info.cullEligible = data->cullEligible;
            info.scal = data->scalability;
            info.bounds = vfx::resolveBounds(data->bounds, *data);
        }
        // A failed load caches info.valid=false so we neither cull nor re-read the file on
        // every spawn; the actual missing-asset warning is emitted by spawnStep's create path.
        return childCache.emplace(path, std::move(info)).first->second;
    }

    VFXSequenceRuntimeServiceImpl::CachedCull VFXSequenceRuntimeServiceImpl::queryCullState() const
    {
        // EventDispatcher::query throws when no renderer registered the handler (headless /
        // no graphics). Treat any failure as "no cull state" so combos never cull.
        try
        {
            const auto r = ::events::EventDispatcher::instance().query(events::vfxruntime::GetVFXCullStateQuery{});
            CachedCull c;
            c.valid = r.valid;
            c.viewProj = r.viewProj;
            c.cameraPos = r.cameraPos;
            c.distanceCullEnabled = r.distanceCullEnabled;
            c.maxDrawDistance = r.maxDrawDistance;
            return c;
        }
        catch (const std::exception&)
        {
            return {};
        }
    }

    VFXSequenceRuntimeServiceImpl::CachedTier VFXSequenceRuntimeServiceImpl::queryQualityTier() const
    {
        // Same headless-safe contract as queryCullState: a throw (no renderer) => invalid,
        // which disables the tier scalability gate so nothing is skipped without a renderer.
        try
        {
            const vfx::VFXQualityTier tier =
                ::events::EventDispatcher::instance().query(events::vfxruntime::GetVFXQualityTierQuery{});
            return {true, tier};
        }
        catch (const std::exception&)
        {
            return {};
        }
    }

    void VFXSequenceRuntimeServiceImpl::invalidateSequence(const std::string& path)
    {
        const std::string target = normalizeSequenceCachePath(path);
        if (target.empty())
            return;

        for (auto it = sequenceCache.begin(); it != sequenceCache.end();)
        {
            if (normalizeSequenceCachePath(it->first) == target)
                it = sequenceCache.erase(it);
            else
                ++it;
        }
        // The saved asset may be a child .vfVFX referenced by steps — drop its cached cull
        // metadata too so the next spawn re-reads bounds/cullEligible from disk.
        for (auto it = childCache.begin(); it != childCache.end();)
        {
            if (normalizeSequenceCachePath(it->first) == target)
                it = childCache.erase(it);
            else
                ++it;
        }
    }

    VFXEmitterOverrides VFXSequenceRuntimeServiceImpl::toOverrides(const vfx::VFXSequenceStep& step)
    {
        return services::toEmitterOverrides(step.overrides);
    }

    glm::mat4 VFXSequenceRuntimeServiceImpl::resolveComboParent(ComboInstance& combo)
    {
        if (!combo.attached || !combo.socketEntity.isValid() || combo.attachSocket.empty())
            return combo.parentTransform;

        auto& dispatcher = ::events::EventDispatcher::instance();
        ::events::socket::HasSocketQuery hasQuery;
        hasQuery.entity = combo.socketEntity;
        hasQuery.socketName = combo.attachSocket;
        if (!dispatcher.query(hasQuery))
        {
            if (!combo.socketWarned)
            {
                reportWarning("[VFXSequence] combo " + std::to_string(combo.id) + " socket '" +
                              combo.attachSocket + "' not found; using last known transform");
                combo.socketWarned = true;
            }
            return combo.parentTransform;
        }

        ::events::socket::GetSocketWorldTransformQuery xformQuery;
        xformQuery.parentEntity = combo.socketEntity;
        xformQuery.socketName = combo.attachSocket;
        return dispatcher.query(xformQuery);
    }

    glm::mat4 VFXSequenceRuntimeServiceImpl::resolveStepParent(ComboInstance& combo, const ActiveStep& step,
                                                               const glm::mat4& comboParent)
    {
        if (!step.def || step.def->socketName.empty() || !combo.socketEntity.isValid())
            return comboParent;

        auto& dispatcher = ::events::EventDispatcher::instance();
        ::events::socket::HasSocketQuery hasQuery;
        hasQuery.entity = combo.socketEntity;
        hasQuery.socketName = step.def->socketName;
        if (!dispatcher.query(hasQuery))
            return comboParent;

        ::events::socket::GetSocketWorldTransformQuery xformQuery;
        xformQuery.parentEntity = combo.socketEntity;
        xformQuery.socketName = step.def->socketName;
        return dispatcher.query(xformQuery);
    }

    glm::mat4 VFXSequenceRuntimeServiceImpl::composeStepWorldTransform(const ActiveStep& step,
                                                                       const glm::mat4& stepParent) const
    {
        if (!step.def)
            return stepParent;

        glm::mat4 local = vfx::VFXComboTimeline::composeStepLocal(*step.def);
        if (step.payload && step.payload->position)
            local = glm::translate(glm::mat4(1.0f), *step.payload->position) * local;
        return stepParent * local;
    }

    void VFXSequenceRuntimeServiceImpl::spawnStep(ComboInstance& combo, int stepIndex, const glm::mat4& stepParent,
                                                  const vfx::VFXCuePayload* payload)
    {
        ActiveStep& step = combo.steps[static_cast<size_t>(stepIndex)];
        step.spawned = true; // mark regardless of outcome so we never retry a failed spawn
        if (!step.def)
            return;

        // VK-1496 — spawnStep only ever creates VFX children. Sound/ScriptCue (and unknown)
        // kinds have no persistent child; their side effects are dispatched from
        // applyComboEvents on the forward path only. This guard also protects replayTo()'s
        // DIRECT spawnStep calls (seek/prewarm), keeping typed steps silent during scrub and
        // suppressing the spurious "missing .vfVFX" warning below.
        if (step.def->kind != vfx::VFXStepKind::VFX)
            return;

        if (payload)
            step.payload = *payload;
        else
            step.payload.reset();

        const std::string path = step.def->vfxRef.resolve();
        if (path.empty())
        {
            reportWarning("[VFXSequence] combo " + std::to_string(combo.id) + " step '" +
                          step.def->label + "' has an unresolved/missing .vfVFX asset; skipping");
            return;
        }

        // Compose the child's world transform once — reused for the cull test and the spawn.
        const glm::mat4 stepWorld = composeStepWorldTransform(step, stepParent);

        // VK-1453 — pre-spawn gate. Two independent skips, each of which marks the step
        // spawned (never retried) with no child, no warning, and ++culledSpawns:
        //   (1) tier-disabled — GLOBAL: the child's scalability profile is renderer-disabled
        //       at the active quality tier. The renderer returns id 0 for such an instance,
        //       which would otherwise log a false "failed create", so we must skip it for
        //       EVERY step (looping, socketed and attached included).
        //   (2) frustum/distance cull — fire-and-forget only (non-looping, non-socket step of
        //       a non-attached combo; combo children are never camera-relative).
        // We only read the child asset once a renderer is present (a valid cull state OR tier);
        // headless/no-provider leaves both invalid => no disk read => byte-identical behavior.
        if (combo.cachedCull.valid || combo.cachedTier.valid)
        {
            const ChildVFXInfo& childInfo = loadChildInfo(path);
            if (childInfo.valid)
            {
                // (1) Tier scalability gate — camera-independent, applies to all steps.
                if (combo.cachedTier.valid && childInfo.scal.enabled)
                {
                    const vfx::VFXScalabilityLevel level =
                        vfx::resolveScalability(childInfo.scal, combo.cachedTier.tier);
                    if (!level.rendererEnabled)
                    {
                        ++culledSpawns; // step.spawned already true; leave childId=0, no warning
                        return;
                    }
                }

                // (2) Frustum / distance cull — fire-and-forget only, needs a valid cull state.
                const bool structurallyCullable =
                    !step.def->loop && step.def->socketName.empty() && !combo.attached;
                if (structurallyCullable && childInfo.cullEligible && combo.cachedCull.valid)
                {
                    const math::AABB worldBounds = childInfo.bounds.getTransformed(stepWorld);
                    math::Frustum frustum;
                    frustum.extractFromMatrix(combo.cachedCull.viewProj);
                    bool culled = !frustum.intersectsAABB(worldBounds);
                    if (!culled && combo.cachedCull.distanceCullEnabled && combo.cachedCull.maxDrawDistance > 0.0f)
                    {
                        const float dist = glm::distance(worldBounds.getCenter(), combo.cachedCull.cameraPos);
                        culled = dist > combo.cachedCull.maxDrawDistance;
                    }
                    if (culled)
                    {
                        ++culledSpawns; // step.spawned already true; leave childId=0, emit no warning
                        return;
                    }
                }
            }
        }

        auto& dispatcher = ::events::EventDispatcher::instance();
        events::vfxruntime::CreateVFXInstanceCommand createCmd;
        createCmd.params.vfxAssetPath = path;
        createCmd.params.worldTransform = stepWorld;
        createCmd.params.loop = step.def->loop;
        createCmd.params.autoDestroy = !step.def->loop; // non-looping children self-destruct when done
        createCmd.params.priority = VFXEmitterPriority::Normal;
        createCmd.params.cameraRelative = false;
        createCmd.params.seed = combo.timeline.derivedSeed(stepIndex); // VK-1451 deterministic child seed
        // VK-1460: combo children must NOT use the renderer's dormant instance pool. A combo
        // retains step.childId across frames and later drives/reaps/destroys it, but the pool
        // re-issues the same instance id to a new owner (VFXHandlePool has no generation tag),
        // so a revived id would be driven/destroyed by the wrong combo (teleport / early
        // vanish / leaked combo). Fresh ids are monotonic (VFXSceneRenderer nextInstanceId++),
        // so a non-poolable child gets a unique, never-reused id and the aliasing is
        // structurally impossible. Fire-and-forget spawns (VFX.spawnAt) keep pooling — they
        // never retain the id, which is the pool's intended "fire-and-forget only" use.
        createCmd.params.poolable = false;

        const VFXInstanceId childId = dispatcher.execute(createCmd);
        if (childId == 0)
        {
            reportWarning("[VFXSequence] combo " + std::to_string(combo.id) + " step '" +
                          step.def->label + "' failed to create instance");
            return;
        }

        VFXEmitterOverrides overrides = toOverrides(*step.def);
        if (payload)
        {
            if (payload->color)
                overrides.startColor = *payload->color;
            for (const auto& overrideValue : payload->custom)
                services::applyOverride(overrides, overrideValue);
        }
        events::vfxruntime::ApplyVFXInstanceOverridesCommand ovCmd;
        ovCmd.instanceId = childId;
        ovCmd.overrides = overrides;
        dispatcher.execute(ovCmd);

        events::vfxruntime::PlayVFXInstanceCommand playCmd;
        playCmd.instanceId = childId;
        dispatcher.execute(playCmd);

        step.childId = childId;
    }

    VFXInstanceId VFXSequenceRuntimeServiceImpl::spawnEventChild(ComboInstance& combo, int receivingStepIndex,
                                                                 const vfx::VFXEventPayload& payload)
    {
        const ActiveStep& recv = combo.steps[static_cast<size_t>(receivingStepIndex)];
        if (!recv.def)
            return 0;

        const std::string path = recv.def->vfxRef.resolve();
        if (path.empty())
        {
            reportWarning("[VFXSequence] combo " + std::to_string(combo.id) + " StepOutput step '" +
                          recv.def->label + "' has an unresolved/missing .vfVFX asset; skipping");
            return 0;
        }

        // INV-3 world anchoring: identity parent + the event's world position + the receiver's
        // authored local offset. NOT the combo/socket parent — the event position is already
        // world-space, so applying comboParent again would double-transform it.
        const glm::vec3 anchor = payload.position.value_or(glm::vec3(0.0f));
        const glm::mat4 stepWorld =
            glm::translate(glm::mat4(1.0f), anchor) * vfx::VFXComboTimeline::composeStepLocal(*recv.def);

        // Reuse spawnStep's pre-spawn cull/tier gate (combo.cachedCull/Tier are stamped by
        // drainParticleEvents before this call). Event-children are non-looping/non-socket =>
        // structurally cullable, so the fire-and-forget frustum/distance leg applies.
        if (combo.cachedCull.valid || combo.cachedTier.valid)
        {
            const ChildVFXInfo& childInfo = loadChildInfo(path);
            if (childInfo.valid)
            {
                if (combo.cachedTier.valid && childInfo.scal.enabled)
                {
                    const vfx::VFXScalabilityLevel level =
                        vfx::resolveScalability(childInfo.scal, combo.cachedTier.tier);
                    if (!level.rendererEnabled)
                    {
                        ++culledSpawns;
                        return 0;
                    }
                }
                if (childInfo.cullEligible && combo.cachedCull.valid)
                {
                    const math::AABB worldBounds = childInfo.bounds.getTransformed(stepWorld);
                    math::Frustum frustum;
                    frustum.extractFromMatrix(combo.cachedCull.viewProj);
                    bool culled = !frustum.intersectsAABB(worldBounds);
                    if (!culled && combo.cachedCull.distanceCullEnabled && combo.cachedCull.maxDrawDistance > 0.0f)
                    {
                        const float dist = glm::distance(worldBounds.getCenter(), combo.cachedCull.cameraPos);
                        culled = dist > combo.cachedCull.maxDrawDistance;
                    }
                    if (culled)
                    {
                        ++culledSpawns;
                        return 0;
                    }
                }
            }
        }

        auto& dispatcher = ::events::EventDispatcher::instance();
        events::vfxruntime::CreateVFXInstanceCommand createCmd;
        createCmd.params.vfxAssetPath = path;
        createCmd.params.worldTransform = stepWorld;
        createCmd.params.loop = false;
        createCmd.params.autoDestroy = true; // fire-and-forget; self-destructs when done
        createCmd.params.priority = VFXEmitterPriority::Normal;
        createCmd.params.cameraRelative = false;
        // Distinct-but-varied seed: the receiver's step seed perturbed by how many event-children it
        // has already spawned, so repeated impacts don't look identical. Never 0 (renderer-random).
        uint32_t seed = combo.timeline.derivedSeed(receivingStepIndex) ^
                        (static_cast<uint32_t>(combo.eventChildren.size()) * 2654435761u);
        if (seed == 0)
            seed = 1u;
        createCmd.params.seed = seed;
        // Non-poolable for the same reason as spawnStep: a retained id must never be re-issued.
        createCmd.params.poolable = false;

        const VFXInstanceId childId = dispatcher.execute(createCmd);
        if (childId == 0)
        {
            reportWarning("[VFXSequence] combo " + std::to_string(combo.id) + " StepOutput step '" +
                          recv.def->label + "' failed to create instance");
            return 0;
        }

        // Authored step overrides + inherit-gated impact payload. Position is the anchor (already in
        // the transform); velocity maps to emit direction + start speed, color to start color, and
        // scalar to start size — the fields the services override API exposes.
        VFXEmitterOverrides overrides = toOverrides(*recv.def);
        if (recv.def->inheritColor && payload.color)
            overrides.startColor = *payload.color;
        if (recv.def->inheritScalar && payload.scalar)
            overrides.startSize = *payload.scalar;
        if (recv.def->inheritVelocity && payload.velocity)
        {
            const glm::vec3 v = *payload.velocity;
            const float speed = glm::length(v);
            if (speed > 1e-4f)
            {
                overrides.emitDirection = v / speed;
                overrides.startSpeed = speed;
            }
        }
        for (const auto& overrideValue : payload.custom)
            services::applyOverride(overrides, overrideValue);
        events::vfxruntime::ApplyVFXInstanceOverridesCommand ovCmd;
        ovCmd.instanceId = childId;
        ovCmd.overrides = overrides;
        dispatcher.execute(ovCmd);

        events::vfxruntime::PlayVFXInstanceCommand playCmd;
        playCmd.instanceId = childId;
        dispatcher.execute(playCmd);

        return childId;
    }

    void VFXSequenceRuntimeServiceImpl::drainParticleEvents()
    {
        std::vector<PendingParticleEvent> events;
        {
            std::lock_guard<std::mutex> lock(particleEventMutex);
            if (pendingParticleEvents.empty())
                return;
            events.swap(pendingParticleEvents);
        }
        if (combos.empty())
            return;

        // Only reached when there are events to route: snapshot the cull/tier state and stamp every
        // active combo so spawnEventChild reuses the pre-spawn cull/tier gate.
        const CachedCull cull = queryCullState();
        const CachedTier tier = queryQualityTier();

        // Transient reverse index: source childId -> (comboId, sourceStepIndex). A source is a live
        // (or just-retired within grace) step declaring an outputEventName, in an ACTIVE combo (a
        // stopped/paused/culled combo must not spawn new receivers). No persistent map => no stale-id
        // leak; combo child ids are monotonic + non-poolable => no reuse can mis-route (INV-4/INV-5).
        std::unordered_map<VFXInstanceId, std::pair<VFXComboInstanceId, int>> sourceIndex;
        for (auto& [comboId, combo] : combos)
        {
            if (!combo.playing || combo.paused || combo.comboCulled)
                continue;
            combo.cachedCull = cull;
            combo.cachedTier = tier;
            for (size_t i = 0; i < combo.steps.size(); ++i)
            {
                const ActiveStep& step = combo.steps[i];
                if (step.childId != 0 && step.def && !step.def->outputEventName.empty())
                    sourceIndex[step.childId] = {comboId, static_cast<int>(i)};
            }
            for (const auto& rs : combo.retiredSourceGrace)
            {
                if (rs.childId != 0)
                    sourceIndex[rs.childId] = {comboId, rs.stepIndex};
            }
        }
        if (sourceIndex.empty())
            return;

        // Per-drain per-receiver spawn counter for the EveryEvent per-tick cap (key = comboId|r).
        std::unordered_map<uint64_t, uint32_t> spawnsThisTick;

        for (const auto& e : events)
        {
            auto srcIt = sourceIndex.find(e.parentInstanceId);
            if (srcIt == sourceIndex.end())
                continue; // stale / non-source / inactive combo => drop (structurally correct)
            const VFXComboInstanceId comboId = srcIt->second.first;
            const int srcIdx = srcIt->second.second;
            auto comboIt = combos.find(comboId);
            if (comboIt == combos.end())
                continue; // combo destroyed this frame; its receivers went with it
            ComboInstance& combo = comboIt->second;
            const vfx::VFXSequenceStep* srcDef = combo.steps[static_cast<size_t>(srcIdx)].def;
            if (!srcDef)
                continue;
            if (e.eventType != static_cast<uint32_t>(srcDef->outputEventType))
                continue; // wrong particle event type for this source's declared output

            const std::string outName = srcDef->outputEventName;

            // World-space payload (INV-3): position anchors the spawn; the rest is inherit-gated.
            vfx::VFXEventPayload payload;
            payload.position = e.position;
            payload.velocity = e.velocity;
            payload.color = glm::vec4(e.color, 1.0f);
            payload.scalar = e.size;

            for (size_t r = 0; r < combo.steps.size(); ++r)
            {
                const vfx::VFXSequenceStep* recvDef = combo.steps[r].def;
                if (!recvDef || recvDef->trigger != vfx::VFXStepTrigger::StepOutput)
                    continue;
                if (recvDef->sourceStepIndex != srcIdx || recvDef->sourceEventName != outName)
                    continue;
                // VK-1497 — honor the per-step play/probability/variant mask the timeline resolved
                // for this run; a receiver rolled out of this run must not fire on source events.
                // Timeline-driven paths and the editor preview already guard with !plays_[i] — review #4.
                if (!combo.timeline.isPlaying(static_cast<int>(r)))
                    continue;

                if (recvDef->eventConsumption == vfx::VFXEventConsumption::FirstEvent)
                {
                    if (combo.firstEventConsumed[r])
                        continue;
                    combo.firstEventConsumed[r] = true;
                    const VFXInstanceId child = spawnEventChild(combo, static_cast<int>(r), payload);
                    if (child != 0)
                        combo.eventChildren.push_back(
                            ComboInstance::EventChild{child, static_cast<int>(r), false});
                }
                else // EveryEvent — bounded by the per-receiver live budget + per-tick cap.
                {
                    uint32_t liveForR = 0;
                    for (const auto& ec : combo.eventChildren)
                        if (ec.receivingStepIndex == static_cast<int>(r))
                            ++liveForR;
                    const uint64_t key = (static_cast<uint64_t>(comboId) << 32) | static_cast<uint32_t>(r);
                    uint32_t& perTick = spawnsThisTick[key];
                    if (liveForR >= recvDef->eventBudget || perTick >= kMaxEventSpawnsPerReceiverPerTick)
                    {
                        reportWarning("[VFXSequence] combo " + std::to_string(comboId) +
                                      " StepOutput receiver hit its event budget; dropping an event spawn");
                        continue;
                    }
                    const VFXInstanceId child = spawnEventChild(combo, static_cast<int>(r), payload);
                    if (child != 0)
                    {
                        combo.eventChildren.push_back(
                            ComboInstance::EventChild{child, static_cast<int>(r), false});
                        ++perTick;
                    }
                }
            }
        }
    }

    void VFXSequenceRuntimeServiceImpl::fireSoundStep(ComboInstance& combo, int stepIndex,
                                                      const glm::mat4& stepParent)
    {
        ActiveStep& step = combo.steps[static_cast<size_t>(stepIndex)];
        step.spawned = true; // one-shot: mark fired so it is never retried
        if (!step.def)
            return;

        const std::string path = step.def->audioRef.resolve();
        if (path.empty())
        {
            reportWarning("[VFXSequence] combo " + std::to_string(combo.id) + " sound step '" +
                          step.def->label + "' has an unresolved/missing .vfAudio asset; skipping");
            return;
        }

        // VK-1496 fire-and-forget one-shot: the AudioHandle is intentionally discarded — the
        // sound is owned by the audio service and outlives the combo; loop / StopAfterDuration
        // truncation / following a moving combo are out of scope this pass (see VK-1504). The
        // audio service may be absent (Tests / headless): EventDispatcher::execute throws with
        // no handler, so the dispatch is guarded exactly like queryCullState() — tightly, so a
        // missing handler cannot skip sibling steps dispatched in the same tick.
        auto& dispatcher = ::events::EventDispatcher::instance();
        try
        {
            if (step.def->spatialized)
            {
                ::events::audio::PlaySound3DCommand cmd; // audio events live in global ::events::audio
                cmd.path = path;
                cmd.position = glm::vec3(composeStepWorldTransform(step, stepParent)[3]);
                cmd.params.volume = step.def->volume;
                cmd.params.pitch = step.def->pitch;
                cmd.params.loop = false;
                cmd.params.is3D = true;
                dispatcher.execute(cmd);
            }
            else
            {
                ::events::audio::PlayStreamingSoundCommand cmd;
                cmd.path = path;
                cmd.params.volume = step.def->volume;
                cmd.params.pitch = step.def->pitch;
                cmd.params.loop = false;
                cmd.params.is3D = false;
                dispatcher.execute(cmd);
            }
        }
        catch (const std::exception&)
        {
            // No audio handler registered (headless / Tests). Silently skip — same contract as
            // queryCullState()'s "no renderer => no cull".
        }
    }

    void VFXSequenceRuntimeServiceImpl::fireScriptCueStep(ComboInstance& combo, int stepIndex)
    {
        ActiveStep& step = combo.steps[static_cast<size_t>(stepIndex)];
        step.spawned = true; // one-shot
        if (!step.def || step.def->emitCueName.empty())
            return;

        // Publish the authored cue + payload on the same channel VK-1495's ScriptVFXEventBridge
        // forwards to mType onComboCue. publish() is fire-and-forget pub/sub and does NOT throw
        // on zero subscribers, so no try/catch is needed. Publish-only: we deliberately do NOT
        // call timeline.fireCue(), so a ScriptCue step never relays into other combo steps.
        publishCueFired(combo, step.def->emitCueName, step.def->cuePayload);
    }

    VFXComboInstanceId VFXSequenceRuntimeServiceImpl::createCombo(const std::string& sequenceAssetPath,
                                                                  const glm::mat4& worldTransform,
                                                                  uint32_t entityId, bool autoDestroyOnFinish,
                                                                  uint32_t seed, float prewarm,
                                                                  float playbackRate, float fixedStep,
                                                                  bool loopSequence)
    {
        auto data = loadSequence(sequenceAssetPath);
        if (!data || data->steps.empty())
        {
            reportWarning("[VFXSequence] cannot create combo from '" + sequenceAssetPath +
                          "' (load failed or no steps)");
            return 0;
        }

        const VFXComboInstanceId id = nextComboId++;
        ComboInstance combo;
        combo.id = id;
        combo.data = data;
        combo.parentTransform = worldTransform;
        combo.autoDestroyOnFinish = autoDestroyOnFinish;
        // VK-1498 — whole-sequence loop is a per-placement decision (from the component `loop`);
        // stableLoop is an intrinsic authoring property of the sequence content, read once here.
        combo.loopSequence = loopSequence;
        combo.stableLoop = data->stableLoop;
        if (entityId != 0)
            combo.socketEntity = EntityHandle{static_cast<uint64_t>(entityId)};

        // VK-1451 timeline controls — command value overrides the asset's, asset is the
        // default, and a still-zero seed is auto-randomized once here so playback varies
        // while staying stable for the life of the combo.
        uint32_t effectiveSeed = seed != 0 ? seed : data->seed;
        if (effectiveSeed == 0)
        {
            static std::random_device seedRd;
            static std::mt19937 seedGen(seedRd());
            effectiveSeed = std::uniform_int_distribution<uint32_t>{}(seedGen);
            if (effectiveSeed == 0)
                effectiveSeed = 1u;
        }
        combo.seed = effectiveSeed;
        combo.playbackRate = playbackRate >= 0.0f ? playbackRate : data->playbackRate;
        if (combo.playbackRate <= 0.0f)
            combo.playbackRate = 1.0f;
        combo.fixedStep = fixedStep >= 0.0f ? fixedStep : data->fixedStep;
        combo.prewarm = prewarm >= 0.0f ? prewarm : data->prewarm;

        combo.steps.reserve(data->steps.size());
        for (const auto& stepDef : data->steps)
            combo.steps.push_back(ActiveStep{&stepDef, 0, false, false});
        combo.firstEventConsumed.assign(data->steps.size(), false); // VK-1524 FirstEvent latches

        combo.timeline.reset(*data, combo.seed);

        combos.emplace(id, std::move(combo));
        return id;
    }

    void VFXSequenceRuntimeServiceImpl::destroyCombo(ComboInstance& combo)
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        for (auto& step : combo.steps)
        {
            if (step.childId != 0)
            {
                events::vfxruntime::DestroyVFXInstanceCommand destroyCmd;
                destroyCmd.instanceId = step.childId;
                dispatcher.execute(destroyCmd);
                step.childId = 0;
                step.payload.reset();
            }
        }
        // VK-1524 — destroy any live event-children so they never outlive the combo (they are
        // separate instances, not tracked by step.childId).
        for (auto& ec : combo.eventChildren)
        {
            if (ec.childId != 0)
            {
                events::vfxruntime::DestroyVFXInstanceCommand destroyCmd;
                destroyCmd.instanceId = ec.childId;
                dispatcher.execute(destroyCmd);
            }
        }
        combo.eventChildren.clear();
    }

    void VFXSequenceRuntimeServiceImpl::clearEventState(ComboInstance& combo)
    {
        // VK-1524 — destroyCombo() already destroys + clears eventChildren; reset the schedule-scoped
        // StepOutput state so a reset/replay/loop re-arms FirstEvent latches and drops stale grace.
        std::fill(combo.firstEventConsumed.begin(), combo.firstEventConsumed.end(), false);
        combo.retiredSourceGrace.clear();
    }

    void VFXSequenceRuntimeServiceImpl::destroyCombo(VFXComboInstanceId id)
    {
        // VK-1496 — if a combo reference/iterator is currently held (inside update() or
        // triggerCue(), reachable re-entrantly from a ScriptCue's onComboCue), defer the erase
        // so it never invalidates the live iterator; drainPendingComboTeardowns() applies it.
        if (comboTeardownGuard > 0)
        {
            pendingComboTeardowns.emplace_back(id, false);
            return;
        }
        auto it = combos.find(id);
        if (it == combos.end())
            return;
        destroyCombo(it->second);
        combos.erase(it);
    }

    void VFXSequenceRuntimeServiceImpl::playCombo(VFXComboInstanceId id)
    {
        auto it = combos.find(id);
        if (it == combos.end())
            return;
        ComboInstance& combo = it->second;
        combo.playing = true;

        // VK-1451 — apply the authored prewarm once, the first time the combo starts.
        if (!combo.prewarmApplied)
        {
            combo.prewarmApplied = true;
            if (combo.prewarm > 0.0f && combo.timeline.elapsed() == 0.0f)
                replayTo(combo, combo.prewarm);
        }
    }

    void VFXSequenceRuntimeServiceImpl::stopCombo(VFXComboInstanceId id)
    {
        auto it = combos.find(id);
        if (it == combos.end())
            return;
        ComboInstance& combo = it->second;
        combo.playing = false;

        auto& dispatcher = ::events::EventDispatcher::instance();
        for (auto& step : combo.steps)
        {
            if (step.childId == 0)
                continue;
            if (step.def && step.def->loop)
            {
                // Looping children never self-destruct — destroy them explicitly.
                events::vfxruntime::DestroyVFXInstanceCommand destroyCmd;
                destroyCmd.instanceId = step.childId;
                dispatcher.execute(destroyCmd);
                step.childId = 0;
                step.payload.reset();
            }
            else
            {
                events::vfxruntime::StopVFXInstanceCommand stopCmd;
                stopCmd.instanceId = step.childId;
                dispatcher.execute(stopCmd);
            }
            step.stopped = true;
        }
    }

    void VFXSequenceRuntimeServiceImpl::resetCombo(VFXComboInstanceId id)
    {
        // VK-1496 — defer while a combo reference/iterator is held (see comboTeardownGuard),
        // so a reset requested re-entrantly from onComboCue can't mutate a combo mid-iteration.
        if (comboTeardownGuard > 0)
        {
            pendingComboTeardowns.emplace_back(id, true);
            return;
        }
        auto it = combos.find(id);
        if (it == combos.end())
            return;
        ComboInstance& combo = it->second;
        destroyCombo(combo);
        clearEventState(combo); // VK-1524 — re-arm FirstEvent latches, drop grace
        combo.playing = false;
        combo.accumulator = 0.0f;
        combo.prewarmApplied = false;
        combo.timeline.rewind();
        for (auto& step : combo.steps)
        {
            step.spawned = false;
            step.stopped = false;
            step.childId = 0;
            step.payload.reset();
        }
    }

    void VFXSequenceRuntimeServiceImpl::drainPendingComboTeardowns()
    {
        // Runs with comboTeardownGuard == 0, so destroyCombo/resetCombo now execute immediately.
        // Swap first so the vector is stable while we iterate. A combo already erased by the
        // update() auto-destroy path is a safe no-op (find fails inside destroyCombo/resetCombo).
        std::vector<std::pair<VFXComboInstanceId, bool>> pending;
        pending.swap(pendingComboTeardowns);
        for (const auto& [id, isReset] : pending)
        {
            if (isReset)
                resetCombo(id);
            else
                destroyCombo(id);
        }
    }

    void VFXSequenceRuntimeServiceImpl::setComboTransform(VFXComboInstanceId id, const glm::mat4& worldTransform)
    {
        auto it = combos.find(id);
        if (it == combos.end())
            return;
        it->second.parentTransform = worldTransform;
    }

    void VFXSequenceRuntimeServiceImpl::attachComboToSocket(VFXComboInstanceId id, EntityHandle entity,
                                                            const std::string& socketName)
    {
        auto it = combos.find(id);
        if (it == combos.end())
            return;
        ComboInstance& combo = it->second;
        combo.socketEntity = entity;
        combo.attachSocket = socketName;
        combo.attached = entity.isValid() && !socketName.empty();
        combo.socketWarned = false;
    }

    void VFXSequenceRuntimeServiceImpl::detachCombo(VFXComboInstanceId id)
    {
        auto it = combos.find(id);
        if (it == combos.end())
            return;
        it->second.attached = false;
        it->second.attachSocket.clear();
    }

    void VFXSequenceRuntimeServiceImpl::triggerCue(VFXComboInstanceId id, const std::string& cueName,
                                                   const vfx::VFXCuePayload& payload)
    {
        auto it = combos.find(id);
        if (it == combos.end() || cueName.empty())
            return;
        ComboInstance& combo = it->second;
        const glm::mat4 comboParent = resolveComboParent(combo);
        std::vector<vfx::ComboEvent> events;
        combo.timeline.fireCue(cueName, events);
        // VK-1496 — guard the reference held across the (script-invoking) publish/apply so a
        // re-entrant Destroy/Reset of this combo is deferred, not applied to `it` mid-call.
        // RAII so a throwing publish/apply cannot leave the guard stuck >0 (review #11); the
        // drain is try-guarded because it may run while an exception is unwinding.
        ++comboTeardownGuard;
        ScopeExit teardownGuard([this] {
            if (--comboTeardownGuard == 0)
            {
                try { drainPendingComboTeardowns(); } catch (...) {}
            }
        });
        publishCueFired(combo, cueName, payload);
        applyComboEvents(combo, events, comboParent, &payload);
    }

    bool VFXSequenceRuntimeServiceImpl::isComboPlaying(VFXComboInstanceId id) const
    {
        auto it = combos.find(id);
        return it != combos.end() && it->second.playing;
    }

    void VFXSequenceRuntimeServiceImpl::applyComboEvents(ComboInstance& combo,
                                                         const std::vector<vfx::ComboEvent>& events,
                                                         const glm::mat4& comboParent,
                                                         const vfx::VFXCuePayload* manualPayload)
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        for (const auto& ev : events)
        {
            if (ev.stepIndex < 0 || ev.stepIndex >= static_cast<int>(combo.steps.size()))
                continue;
            ActiveStep& step = combo.steps[static_cast<size_t>(ev.stepIndex)];
            if (ev.kind == vfx::ComboEventKind::SpawnStep)
            {
                // VK-1496 — fan out by step kind. THIS BRANCH is the single home for typed
                // side effects. It is reached ONLY from forward paths — update() (:837) and
                // manual triggerCue() (:643); replayTo()/seek call spawnStep() directly and
                // never reach here, so Sound/ScriptCue stay silent on scrub/seek/prewarm. Do
                // NOT move the kind dispatch into spawnStep() or a future spawn-caller
                // refactor could make seek audible (mirror the transport-safety note in
                // ScriptVFXEventBridge.cpp).
                const glm::mat4 stepParent = resolveStepParent(combo, step, comboParent);
                switch (step.def ? step.def->kind : vfx::VFXStepKind::VFX)
                {
                case vfx::VFXStepKind::Sound:
                    fireSoundStep(combo, ev.stepIndex, stepParent);
                    break;
                case vfx::VFXStepKind::ScriptCue:
                    fireScriptCueStep(combo, ev.stepIndex);
                    break;
                case vfx::VFXStepKind::VFX:
                default: // unknown future kinds self-no-op via spawnStep's guard
                {
                    const vfx::VFXCuePayload* payload = manualPayload;
                    if (!payload && ev.sourceMarker >= 0 && combo.data &&
                        ev.sourceMarker < static_cast<int>(combo.data->eventMarkers.size()))
                    {
                        payload = &combo.data->eventMarkers[static_cast<size_t>(ev.sourceMarker)].payload;
                    }
                    spawnStep(combo, ev.stepIndex, stepParent, payload);
                    break;
                }
                }
            }
            else // StopStep — the timeline already gated StopAfterDuration + duration + threshold.
            {
                if (step.childId != 0)
                {
                    events::vfxruntime::StopVFXInstanceCommand stopCmd;
                    stopCmd.instanceId = step.childId;
                    dispatcher.execute(stopCmd);
                }
                step.stopped = true;
            }
        }
    }

    void VFXSequenceRuntimeServiceImpl::publishCueFired(ComboInstance& combo, const std::string& cueName,
                                                        const vfx::VFXCuePayload& payload)
    {
        events::vfxsequence::VFXComboCueFiredNotification notification;
        notification.comboId = combo.id;
        notification.cueName = cueName;
        notification.payload = payload;
        ::events::EventDispatcher::instance().publish(notification);
    }

    void VFXSequenceRuntimeServiceImpl::publishNewlyFiredMarkers(ComboInstance& combo, const std::vector<bool>& before)
    {
        if (!combo.data)
            return;

        const std::vector<bool>& after = combo.timeline.firedMarkers();
        const size_t count = std::min(before.size(), after.size());
        for (size_t i = 0; i < count && i < combo.data->eventMarkers.size(); ++i)
        {
            if (!before[i] && after[i])
            {
                const vfx::VFXSequenceEventMarker& marker = combo.data->eventMarkers[i];
                publishCueFired(combo, marker.cueName, marker.payload);
            }
        }
    }

    void VFXSequenceRuntimeServiceImpl::replayTo(ComboInstance& combo, float targetSeconds)
    {
        // Tear down live children and reset the schedule to t=0.
        destroyCombo(combo);
        clearEventState(combo); // VK-1524 — seek/prewarm never routes events; drop latches + grace
        combo.timeline.rewind();
        combo.accumulator = 0.0f;
        for (auto& step : combo.steps)
        {
            step.spawned = false;
            step.stopped = false;
            step.childId = 0;
            step.payload.reset();
        }

        if (targetSeconds <= 0.0f)
            return;

        // Fast-forward the schedule. Mirror update()'s accumulator exactly so a seek to T
        // lands on the same timeline state a real playthrough to T would: advance in whole
        // fixed steps and carry the remainder, so the spawn set is identical.
        std::vector<vfx::ComboEvent> scratch;
        if (combo.fixedStep > 0.0f)
        {
            const long steps = static_cast<long>(std::floor(targetSeconds / combo.fixedStep));
            for (long k = 0; k < steps && k < 1000000; ++k)
                combo.timeline.advance(combo.fixedStep, scratch);
            combo.accumulator = targetSeconds - static_cast<float>(steps) * combo.fixedStep;
        }
        else
        {
            combo.timeline.advance(targetSeconds, scratch);
        }

        std::vector<int> sourceMarkerByStep(combo.steps.size(), -1);
        for (const auto& ev : scratch)
        {
            if (ev.kind == vfx::ComboEventKind::SpawnStep &&
                ev.stepIndex >= 0 && ev.stepIndex < static_cast<int>(sourceMarkerByStep.size()))
            {
                sourceMarkerByStep[static_cast<size_t>(ev.stepIndex)] = ev.sourceMarker;
            }
        }

        // (Re)spawn only the steps that should be live at the target time, seeded. Steps
        // whose StopAfterDuration window already ended are left unspawned (no zombie
        // children); play-to-completion steps that started before T restart here — the
        // documented "schedule replay" limit of GPU runtime seek.
        const glm::mat4 comboParent = resolveComboParent(combo);
        for (int i = 0; i < combo.timeline.stepCount(); ++i)
        {
            if (combo.timeline.isSpawned(i) && !combo.timeline.isStopped(i))
            {
                const vfx::VFXCuePayload* payload = nullptr;
                const int sourceMarker = sourceMarkerByStep[static_cast<size_t>(i)];
                if (sourceMarker >= 0 && combo.data &&
                    sourceMarker < static_cast<int>(combo.data->eventMarkers.size()))
                {
                    payload = &combo.data->eventMarkers[static_cast<size_t>(sourceMarker)].payload;
                }
                spawnStep(combo, i, resolveStepParent(combo, combo.steps[static_cast<size_t>(i)], comboParent), payload);
            }
        }
    }

    void VFXSequenceRuntimeServiceImpl::restartComboForLoop(ComboInstance& combo)
    {
        // VK-1498 — completion is defined as `allStepsSpawned && !anyLive`, so there are no live
        // children to destroy here. Rewind the schedule (stableLoop keeps the exact VK-1497
        // variety) or re-seed for fresh variety, then clear per-step state. The NEXT forward
        // update() advance re-spawns the t=0 steps and re-fires Sound/ScriptCue/markers through
        // applyComboEvents (so cues re-fire once per iteration). Deliberately no child destroy, no
        // direct respawn, no notification publish — this is a pure in-place mutation, which is safe
        // under comboTeardownGuard (it never erases/emplaces the `combos` map).
        if (combo.stableLoop || !combo.data)
        {
            combo.timeline.rewind();
        }
        else
        {
            combo.loopIteration++;
            const uint32_t nextSeed = vfx::VFXComboTimeline::deriveLoopSeed(combo.seed, combo.loopIteration);
            combo.timeline.reset(*combo.data, nextSeed);
        }
        for (auto& step : combo.steps)
        {
            step.spawned = false;
            step.stopped = false;
            step.childId = 0;
            step.frozenByCull = false;
            step.payload.reset();
        }
        // VK-1524 — at completion the combo has no live children (anyLive counts event-children +
        // grace), so eventChildren is already empty; re-arm the FirstEvent latches for the next loop.
        clearEventState(combo);
        combo.accumulator = 0.0f;
        combo.playing = true;
    }

    bool VFXSequenceRuntimeServiceImpl::computeComboCull(const ComboInstance& combo,
                                                        const glm::mat4& comboParent) const
    {
        // VK-1498 — whole-combo bounds cull applies only to looping combos with a valid cull state
        // and authored Fixed (non-degenerate) bounds. A sequence has N child steps rather than one
        // emitter graph, so Auto/degenerate bounds are not cheaply resolvable here => never cull
        // (the combo runs exactly as before — safe default). Reuses the once-per-tick cachedCull
        // snapshot; no extra query.
        if (!combo.loopSequence || !combo.cachedCull.valid || !combo.data)
            return false;

        const vfx::VFXBounds& b = combo.data->bounds;
        const bool fixedUsable = b.mode == vfx::VFXBoundsMode::Fixed &&
                                 (b.extents.x > 0.0f || b.extents.y > 0.0f || b.extents.z > 0.0f);
        if (!fixedUsable)
            return false;

        const math::AABB local(b.center - b.extents, b.center + b.extents);
        const math::AABB world = local.getTransformed(comboParent);
        math::Frustum frustum;
        frustum.extractFromMatrix(combo.cachedCull.viewProj);
        if (!frustum.intersectsAABB(world))
            return true; // outside the frustum

        // Mirror spawnStep's distance leg: only checked when inside the frustum.
        if (combo.cachedCull.distanceCullEnabled && combo.cachedCull.maxDrawDistance > 0.0f)
        {
            const float dist = glm::distance(world.getCenter(), combo.cachedCull.cameraPos);
            return dist > combo.cachedCull.maxDrawDistance;
        }
        return false;
    }

    void VFXSequenceRuntimeServiceImpl::setComboChildrenFrozen(ComboInstance& combo, bool frozen)
    {
        // VK-1498 — non-destructive freeze/thaw of the combo's live children for the whole-combo
        // bounds cull. Stop => renderer active=false (halts BOTH sim and draw); Play => active=true.
        // Never routes through stopCombo(), which DESTROYS looping children. Gated on !stopped so a
        // step the timeline already stopped (StopAfterDuration) is never revived, and tracked via
        // frozenByCull so re-entry Plays back exactly the set that was frozen.
        auto& dispatcher = ::events::EventDispatcher::instance();
        for (auto& step : combo.steps)
        {
            if (step.childId == 0 || step.stopped)
                continue;
            if (frozen)
            {
                events::vfxruntime::StopVFXInstanceCommand stopCmd;
                stopCmd.instanceId = step.childId;
                dispatcher.execute(stopCmd);
                step.frozenByCull = true;
            }
            else if (step.frozenByCull)
            {
                events::vfxruntime::PlayVFXInstanceCommand playCmd;
                playCmd.instanceId = step.childId;
                dispatcher.execute(playCmd);
                step.frozenByCull = false;
            }
        }
        // VK-1524 — freeze/thaw the world-anchored event-children in lockstep with the step children.
        for (auto& ec : combo.eventChildren)
        {
            if (ec.childId == 0)
                continue;
            if (frozen)
            {
                events::vfxruntime::StopVFXInstanceCommand stopCmd;
                stopCmd.instanceId = ec.childId;
                dispatcher.execute(stopCmd);
                ec.frozenByCull = true;
            }
            else if (ec.frozenByCull)
            {
                events::vfxruntime::PlayVFXInstanceCommand playCmd;
                playCmd.instanceId = ec.childId;
                dispatcher.execute(playCmd);
                ec.frozenByCull = false;
            }
        }
    }

    void VFXSequenceRuntimeServiceImpl::update(float deltaTime)
    {
        // VK-1460: apply AssetSaved invalidations queued from the editor thread here, on the
        // update thread, so sequenceCache/childCache are only mutated where spawnStep reads
        // them. Drained before the empty-combos early-out so a later createCombo sees a
        // fresh cache. Mirrors VFXRuntimeAdapter::update.
        {
            std::vector<std::string> invalidations;
            {
                std::lock_guard<std::mutex> lock(sequenceInvalidationMutex);
                invalidations.swap(pendingSequenceInvalidations);
            }
            for (const auto& p : invalidations)
                invalidateSequence(p);
        }

        // VK-1524 — route marshalled particle events to StepOutput receivers at the TOP of the tick
        // (forward-only), before the deterministic timeline advances and before the reap/completion
        // pass, so a source that emitted last frame is still resolvable this frame.
        drainParticleEvents();

        if (combos.empty())
            return;

        auto& dispatcher = ::events::EventDispatcher::instance();
        std::vector<vfx::ComboEvent> events;

        // VK-1453 — snapshot the renderer's cull state and quality tier once per tick; every
        // combo reads them in spawnStep to pre-cull off-screen steps and skip tier-disabled ones.
        const CachedCull tickCull = queryCullState();
        const CachedTier tickTier = queryQualityTier();

        // VK-1496 — hold combo destroy/reset requests raised re-entrantly by a ScriptCue's
        // onComboCue until the loop releases `it`; applied by drainPendingComboTeardowns() below.
        // RAII so a throwing dispatcher.execute/query in the loop cannot leave the guard stuck
        // >0 (which would defer every future teardown forever and leak combos) — review #11.
        ++comboTeardownGuard;
        ScopeExit teardownGuard([this] {
            if (--comboTeardownGuard == 0)
            {
                try { drainPendingComboTeardowns(); } catch (...) {}
            }
        });
        for (auto it = combos.begin(); it != combos.end();)
        {
            ComboInstance& combo = it->second;
            combo.cachedCull = tickCull;
            combo.cachedTier = tickTier;
            const glm::mat4 comboParent = resolveComboParent(combo);

            // VK-1498 — whole-combo bounds cull. On the cull transition, Stop (freeze+hide) or
            // Play back the live children; while culled, skip the ENTIRE tick (advance, cascade,
            // reap, completion) so an off-screen looping combo stops churning. Never destroyed;
            // resumes on re-entry. Skipping reap is required: a Stopped child reports
            // IsVFXInstancePlaying==false, which would zero its childId and lose it on resume.
            const bool nowCulled = computeComboCull(combo, comboParent);
            if (nowCulled != combo.comboCulled)
            {
                setComboChildrenFrozen(combo, nowCulled);
                combo.comboCulled = nowCulled;
            }
            if (combo.comboCulled)
            {
                ++it;
                continue;
            }

            if (combo.playing && !combo.paused && deltaTime > 0.0f)
            {
                events.clear();
                const float scaled = deltaTime * combo.playbackRate;

                if (combo.fixedStep > 0.0f)
                {
                    combo.accumulator += scaled;
                    // Guard against a spiral-of-death after a huge frame hitch.
                    int guard = 0;
                    while (combo.accumulator >= combo.fixedStep && guard < 4096)
                    {
                        const std::vector<bool> firedBefore = combo.timeline.firedMarkers();
                        combo.timeline.advance(combo.fixedStep, events);
                        publishNewlyFiredMarkers(combo, firedBefore);
                        combo.accumulator -= combo.fixedStep;
                        ++guard;
                    }
                }
                else
                {
                    const std::vector<bool> firedBefore = combo.timeline.firedMarkers();
                    combo.timeline.advance(scaled, events);
                    publishNewlyFiredMarkers(combo, firedBefore);
                }

                applyComboEvents(combo, events, comboParent);
            }

            // Cascade the (possibly socket-driven) transform onto every live child.
            for (auto& step : combo.steps)
            {
                if (step.childId == 0 || !step.def)
                    continue;
                events::vfxruntime::SetVFXInstanceTransformCommand xformCmd;
                xformCmd.instanceId = step.childId;
                xformCmd.worldTransform = composeStepWorldTransform(step, resolveStepParent(combo, step, comboParent));
                dispatcher.execute(xformCmd);
            }

            // Reap non-looping children the provider has auto-destroyed.
            for (auto& step : combo.steps)
            {
                if (step.childId == 0 || !step.def || step.def->loop)
                    continue;
                events::vfxruntime::IsVFXInstancePlayingQuery playingQuery;
                playingQuery.instanceId = step.childId;
                if (!dispatcher.query(playingQuery))
                {
                    // VK-1524 — a source step's final death/collision event arrives a few frames
                    // after its child stops (GPU readback + FIFO latency). Keep its id routable for
                    // a short grace so that trailing event still spawns the bound receiver.
                    if (!step.def->outputEventName.empty())
                    {
                        const int stepIndex = static_cast<int>(&step - combo.steps.data());
                        combo.retiredSourceGrace.push_back(
                            ComboInstance::RetiredSource{step.childId, stepIndex, kSourceGraceFrames});
                    }
                    step.childId = 0;
                    step.payload.reset();
                }
            }

            // VK-1524 — reap finished event-children (world-anchored fire-and-forget instances).
            for (size_t k = 0; k < combo.eventChildren.size();)
            {
                events::vfxruntime::IsVFXInstancePlayingQuery playingQuery;
                playingQuery.instanceId = combo.eventChildren[k].childId;
                if (combo.eventChildren[k].childId == 0 || !dispatcher.query(playingQuery))
                    combo.eventChildren.erase(combo.eventChildren.begin() + static_cast<long>(k));
                else
                    ++k;
            }

            // VK-1524 — age out the just-retired-source grace window.
            for (size_t k = 0; k < combo.retiredSourceGrace.size();)
            {
                if (--combo.retiredSourceGrace[k].framesLeft <= 0)
                    combo.retiredSourceGrace.erase(combo.retiredSourceGrace.begin() + static_cast<long>(k));
                else
                    ++k;
            }

            // Combo is finished once it has played, every step has spawned, and no children remain.
            // VK-1524 — event-children and an active source-grace window also count as "live" so a
            // combo neither auto-destroys while a StepOutput source can still emit nor while an
            // event-child is still playing.
            bool anyLive = !combo.eventChildren.empty() || !combo.retiredSourceGrace.empty();
            for (const auto& step : combo.steps)
            {
                if (step.childId != 0)
                    anyLive = true;
            }
            const bool allSpawned = combo.timeline.allStepsSpawned();

            if (combo.playing && allSpawned && !anyLive)
            {
                // VK-1498 — a looping combo replays its whole schedule instead of idling: the next
                // forward advance re-spawns t=0 steps and re-fires cues (markers once per iteration).
                // Non-looping combos stop and (if autoDestroyOnFinish) are erased below. A restarted
                // combo keeps playing=true / elapsed=0, so the auto-destroy guard never erases it.
                if (combo.loopSequence)
                    restartComboForLoop(combo);
                else
                    combo.playing = false;
            }

            if (!combo.playing && allSpawned && !anyLive && combo.autoDestroyOnFinish &&
                combo.timeline.elapsed() > 0.0f)
            {
                it = combos.erase(it);
                continue;
            }
            ++it;
        }

        // comboTeardownGuard is released (and pending teardowns drained) by teardownGuard's
        // ScopeExit as this function returns, even on an exception — review #11.
    }

    void VFXSequenceRuntimeServiceImpl::setComboPaused(VFXComboInstanceId id, bool paused)
    {
        auto it = combos.find(id);
        if (it == combos.end())
            return;
        it->second.paused = paused;
    }

    void VFXSequenceRuntimeServiceImpl::setComboPlaybackRate(VFXComboInstanceId id, float rate)
    {
        auto it = combos.find(id);
        if (it == combos.end() || rate <= 0.0f)
            return;
        it->second.playbackRate = rate;
    }

    void VFXSequenceRuntimeServiceImpl::seekCombo(VFXComboInstanceId id, float seconds)
    {
        auto it = combos.find(id);
        if (it == combos.end())
            return;
        replayTo(it->second, std::max(0.0f, seconds));
    }

    void VFXSequenceRuntimeServiceImpl::destroyAll()
    {
        for (auto& [id, combo] : combos)
            destroyCombo(combo);
        combos.clear();
    }
}
