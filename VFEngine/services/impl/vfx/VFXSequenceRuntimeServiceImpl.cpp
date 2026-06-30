#include "print/Log.hpp"
#include "VFXSequenceRuntimeServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/vfx/VFXRuntimeEvents.hpp"
#include "../../events/vfx/VFXSequenceRuntimeEvents.hpp"
#include "../../events/physics/SocketEvents.hpp"
#include "../../events/project/ResourceEvents.hpp"
#include "asset/AssetDatabase.hpp"
#include "vfx/VFXOverrideNames.hpp"
#include "vfx/VFXSequenceAsset.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <random>
#include <string_view>
#include <vector>

namespace services
{
    namespace
    {
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
    }

    VFXSequenceRuntimeServiceImpl::~VFXSequenceRuntimeServiceImpl()
    {
        if (assetSavedToken.isValid())
        {
            ::events::EventDispatcher::instance().unsubscribe(assetSavedToken);
            assetSavedToken = {};
        }
    }

    void VFXSequenceRuntimeServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::vfxsequence::CreateVFXComboInstanceCommand>(
            [this](const events::vfxsequence::CreateVFXComboInstanceCommand& cmd)
            {
                return createCombo(cmd.sequenceAssetPath, cmd.worldTransform, cmd.entityId, cmd.autoDestroyOnFinish,
                                   cmd.seed, cmd.prewarm, cmd.playbackRate, cmd.fixedStep);
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
            [this](const events::vfxsequence::TriggerVFXComboCueCommand& cmd) { triggerCue(cmd.comboId, cmd.cueName); });

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

        if (assetSavedToken.isValid())
            dispatcher.unsubscribe(assetSavedToken);
        assetSavedToken = dispatcher.subscribe<::events::resource::AssetSavedNotification>(
            [this](const ::events::resource::AssetSavedNotification& n)
            {
                invalidateSequence(n.filePath);
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
    }

    VFXEmitterOverrides VFXSequenceRuntimeServiceImpl::toOverrides(const vfx::VFXSequenceStep& step)
    {
        VFXEmitterOverrides o;
        namespace names = vfx::overridenames;
        for (const auto& [name, value] : step.scalarOverrides)
        {
            const std::string_view key{name.data(), name.size()};
            if (key == names::spawnRate) o.spawnRate = value;
            else if (key == names::lifetime) o.lifetime = value;
            else if (key == names::startSize) o.startSize = value;
            else if (key == names::startSpeed) o.startSpeed = value;
            else if (key == names::stretchMultiplier) o.stretchMultiplier = value;
            else if (key == names::windStrength) o.windStrength = value;
            else if (key == names::gravityStrength) o.gravityStrength = value;
            else if (key == names::softParticleDistance) o.softParticleDistance = value;
            else if (key == names::lightingInfluence) o.lightingInfluence = value;
            else if (key == names::collisionLifetimeLoss) o.collisionLifetimeLoss = value;
            else if (key == names::coneSpread) o.coneSpread = value;
            else if (key == names::renderMode) o.renderMode = static_cast<int>(value);
            else if (key == names::collisionEnabled) o.collisionEnabled = (value != 0.0f);
        }
        for (const auto& [name, v] : step.vectorOverrides)
        {
            const std::string_view key{name.data(), name.size()};
            if (key == names::emitDirection) o.emitDirection = glm::vec3(v);
            else if (key == names::windDirection) o.windDirection = glm::vec3(v);
            else if (key == names::gravityDirection) o.gravityDirection = glm::vec3(v);
            else if (key == names::shapeDimensions) o.shapeDimensions = glm::vec3(v);
            else if (key == names::startColor) o.startColor = v;
        }
        return o;
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
                vfLogWarning("[VFXSequence] combo {} socket '{}' not found; using last known transform",
                             combo.id, combo.attachSocket);
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

    void VFXSequenceRuntimeServiceImpl::spawnStep(ComboInstance& combo, int stepIndex, const glm::mat4& stepParent)
    {
        ActiveStep& step = combo.steps[static_cast<size_t>(stepIndex)];
        step.spawned = true; // mark regardless of outcome so we never retry a failed spawn
        if (!step.def)
            return;

        const std::string path = step.def->vfxRef.resolve();
        if (path.empty())
        {
            vfLogWarning("[VFXSequence] combo {} step '{}' has an unresolved/missing .vfVFX asset; skipping",
                         combo.id, step.def->label);
            return;
        }

        auto& dispatcher = ::events::EventDispatcher::instance();
        events::vfxruntime::CreateVFXInstanceCommand createCmd;
        createCmd.params.vfxAssetPath = path;
        createCmd.params.worldTransform = stepParent * vfx::VFXComboTimeline::composeStepLocal(*step.def);
        createCmd.params.loop = step.def->loop;
        createCmd.params.autoDestroy = !step.def->loop; // non-looping children self-destruct when done
        createCmd.params.priority = VFXEmitterPriority::Normal;
        createCmd.params.cameraRelative = false;
        createCmd.params.seed = combo.timeline.derivedSeed(stepIndex); // VK-1451 deterministic child seed

        const VFXInstanceId childId = dispatcher.execute(createCmd);
        if (childId == 0)
        {
            vfLogWarning("[VFXSequence] combo {} step '{}' failed to create instance", combo.id, step.def->label);
            return;
        }

        const VFXEmitterOverrides overrides = toOverrides(*step.def);
        events::vfxruntime::ApplyVFXInstanceOverridesCommand ovCmd;
        ovCmd.instanceId = childId;
        ovCmd.overrides = overrides;
        dispatcher.execute(ovCmd);

        events::vfxruntime::PlayVFXInstanceCommand playCmd;
        playCmd.instanceId = childId;
        dispatcher.execute(playCmd);

        step.childId = childId;
    }

    VFXComboInstanceId VFXSequenceRuntimeServiceImpl::createCombo(const std::string& sequenceAssetPath,
                                                                  const glm::mat4& worldTransform,
                                                                  uint32_t entityId, bool autoDestroyOnFinish,
                                                                  uint32_t seed, float prewarm,
                                                                  float playbackRate, float fixedStep)
    {
        auto data = loadSequence(sequenceAssetPath);
        if (!data || data->steps.empty())
        {
            vfLogWarning("[VFXSequence] cannot create combo from '{}' (load failed or no steps)", sequenceAssetPath);
            return 0;
        }

        const VFXComboInstanceId id = nextComboId++;
        ComboInstance combo;
        combo.id = id;
        combo.data = data;
        combo.parentTransform = worldTransform;
        combo.autoDestroyOnFinish = autoDestroyOnFinish;
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
            }
        }
    }

    void VFXSequenceRuntimeServiceImpl::destroyCombo(VFXComboInstanceId id)
    {
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
        auto it = combos.find(id);
        if (it == combos.end())
            return;
        ComboInstance& combo = it->second;
        destroyCombo(combo);
        combo.playing = false;
        combo.accumulator = 0.0f;
        combo.prewarmApplied = false;
        combo.timeline.rewind();
        for (auto& step : combo.steps)
        {
            step.spawned = false;
            step.stopped = false;
            step.childId = 0;
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

    void VFXSequenceRuntimeServiceImpl::triggerCue(VFXComboInstanceId id, const std::string& cueName)
    {
        auto it = combos.find(id);
        if (it == combos.end() || cueName.empty())
            return;
        ComboInstance& combo = it->second;
        const glm::mat4 comboParent = resolveComboParent(combo);
        std::vector<vfx::ComboEvent> events;
        combo.timeline.fireCue(cueName, events);
        applyComboEvents(combo, events, comboParent);
    }

    bool VFXSequenceRuntimeServiceImpl::isComboPlaying(VFXComboInstanceId id) const
    {
        auto it = combos.find(id);
        return it != combos.end() && it->second.playing;
    }

    void VFXSequenceRuntimeServiceImpl::applyComboEvents(ComboInstance& combo,
                                                         const std::vector<vfx::ComboEvent>& events,
                                                         const glm::mat4& comboParent)
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        for (const auto& ev : events)
        {
            if (ev.stepIndex < 0 || ev.stepIndex >= static_cast<int>(combo.steps.size()))
                continue;
            ActiveStep& step = combo.steps[static_cast<size_t>(ev.stepIndex)];
            if (ev.kind == vfx::ComboEventKind::SpawnStep)
            {
                spawnStep(combo, ev.stepIndex, resolveStepParent(combo, step, comboParent));
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

    void VFXSequenceRuntimeServiceImpl::replayTo(ComboInstance& combo, float targetSeconds)
    {
        // Tear down live children and reset the schedule to t=0.
        destroyCombo(combo);
        combo.timeline.rewind();
        combo.accumulator = 0.0f;
        for (auto& step : combo.steps)
        {
            step.spawned = false;
            step.stopped = false;
            step.childId = 0;
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

        // (Re)spawn only the steps that should be live at the target time, seeded. Steps
        // whose StopAfterDuration window already ended are left unspawned (no zombie
        // children); play-to-completion steps that started before T restart here — the
        // documented "schedule replay" limit of GPU runtime seek.
        const glm::mat4 comboParent = resolveComboParent(combo);
        for (int i = 0; i < combo.timeline.stepCount(); ++i)
        {
            if (combo.timeline.isSpawned(i) && !combo.timeline.isStopped(i))
                spawnStep(combo, i, resolveStepParent(combo, combo.steps[static_cast<size_t>(i)], comboParent));
        }
    }

    void VFXSequenceRuntimeServiceImpl::update(float deltaTime)
    {
        if (combos.empty())
            return;

        auto& dispatcher = ::events::EventDispatcher::instance();
        std::vector<vfx::ComboEvent> events;

        for (auto it = combos.begin(); it != combos.end();)
        {
            ComboInstance& combo = it->second;
            const glm::mat4 comboParent = resolveComboParent(combo);

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
                        combo.timeline.advance(combo.fixedStep, events);
                        combo.accumulator -= combo.fixedStep;
                        ++guard;
                    }
                }
                else
                {
                    combo.timeline.advance(scaled, events);
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
                xformCmd.worldTransform =
                    resolveStepParent(combo, step, comboParent) * vfx::VFXComboTimeline::composeStepLocal(*step.def);
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
                    step.childId = 0;
            }

            // Combo is finished once it has played, every step has spawned, and no children remain.
            bool anyLive = false;
            for (const auto& step : combo.steps)
            {
                if (step.childId != 0)
                    anyLive = true;
            }
            const bool allSpawned = combo.timeline.allStepsSpawned();

            if (combo.playing && allSpawned && !anyLive)
                combo.playing = false;

            if (!combo.playing && allSpawned && !anyLive && combo.autoDestroyOnFinish &&
                combo.timeline.elapsed() > 0.0f)
            {
                it = combos.erase(it);
                continue;
            }
            ++it;
        }
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
