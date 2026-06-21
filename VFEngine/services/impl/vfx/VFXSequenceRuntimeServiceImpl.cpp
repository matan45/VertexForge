#include "print/Log.hpp"
#include "VFXSequenceRuntimeServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/vfx/VFXRuntimeEvents.hpp"
#include "../../events/vfx/VFXSequenceRuntimeEvents.hpp"
#include "../../events/physics/SocketEvents.hpp"
#include "vfx/VFXSequenceAsset.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace services
{
    void VFXSequenceRuntimeServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::vfxsequence::CreateVFXComboInstanceCommand>(
            [this](const events::vfxsequence::CreateVFXComboInstanceCommand& cmd)
            {
                return createCombo(cmd.sequenceAssetPath, cmd.worldTransform, cmd.entityId, cmd.autoDestroyOnFinish);
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

        dispatcher.registerQueryHandler<events::vfxsequence::IsVFXComboInstancePlayingQuery>(
            [this](const events::vfxsequence::IsVFXComboInstancePlayingQuery& q) { return isComboPlaying(q.comboId); });
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

    glm::mat4 VFXSequenceRuntimeServiceImpl::composeStepLocal(const vfx::VFXSequenceStep& step)
    {
        const glm::mat4 t = glm::translate(glm::mat4(1.0f), step.localPosition);
        const glm::mat4 r = glm::mat4_cast(glm::quat(glm::radians(step.localEulerDeg)));
        const glm::mat4 s = glm::scale(glm::mat4(1.0f), step.localScale);
        return t * r * s;
    }

    VFXEmitterOverrides VFXSequenceRuntimeServiceImpl::toOverrides(const vfx::VFXSequenceStep& step)
    {
        VFXEmitterOverrides o;
        for (const auto& [name, value] : step.scalarOverrides)
        {
            if (name == "spawnRate") o.spawnRate = value;
            else if (name == "lifetime") o.lifetime = value;
            else if (name == "startSize") o.startSize = value;
            else if (name == "startSpeed") o.startSpeed = value;
            else if (name == "stretchMultiplier") o.stretchMultiplier = value;
            else if (name == "windStrength") o.windStrength = value;
            else if (name == "gravityStrength") o.gravityStrength = value;
            else if (name == "softParticleDistance") o.softParticleDistance = value;
            else if (name == "lightingInfluence") o.lightingInfluence = value;
            else if (name == "collisionLifetimeLoss") o.collisionLifetimeLoss = value;
            else if (name == "coneSpread") o.coneSpread = value;
            else if (name == "renderMode") o.renderMode = static_cast<int>(value);
            else if (name == "collisionEnabled") o.collisionEnabled = (value != 0.0f);
        }
        for (const auto& [name, v] : step.vectorOverrides)
        {
            if (name == "emitDirection") o.emitDirection = glm::vec3(v);
            else if (name == "windDirection") o.windDirection = glm::vec3(v);
            else if (name == "gravityDirection") o.gravityDirection = glm::vec3(v);
            else if (name == "shapeDimensions") o.shapeDimensions = glm::vec3(v);
            else if (name == "startColor") o.startColor = v;
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

    void VFXSequenceRuntimeServiceImpl::spawnStep(ComboInstance& combo, ActiveStep& step, const glm::mat4& stepParent)
    {
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
        createCmd.params.worldTransform = stepParent * composeStepLocal(*step.def);
        createCmd.params.loop = step.def->loop;
        createCmd.params.autoDestroy = !step.def->loop; // non-looping children self-destruct when done
        createCmd.params.priority = VFXEmitterPriority::Normal;
        createCmd.params.cameraRelative = false;

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
                                                                  uint32_t entityId, bool autoDestroyOnFinish)
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

        combo.steps.reserve(data->steps.size());
        for (const auto& stepDef : data->steps)
            combo.steps.push_back(ActiveStep{&stepDef, 0, false, false});

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
        it->second.playing = true;
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
        combo.elapsed = 0.0f;
        combo.playing = false;
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
        for (auto& step : combo.steps)
        {
            if (step.spawned || !step.def || step.def->cueName != cueName)
                continue;
            spawnStep(combo, step, resolveStepParent(combo, step, comboParent));
        }
    }

    bool VFXSequenceRuntimeServiceImpl::isComboPlaying(VFXComboInstanceId id) const
    {
        auto it = combos.find(id);
        return it != combos.end() && it->second.playing;
    }

    void VFXSequenceRuntimeServiceImpl::update(float deltaTime)
    {
        if (combos.empty())
            return;

        auto& dispatcher = ::events::EventDispatcher::instance();

        for (auto it = combos.begin(); it != combos.end();)
        {
            ComboInstance& combo = it->second;
            const glm::mat4 comboParent = resolveComboParent(combo);

            if (combo.playing)
            {
                combo.elapsed += deltaTime;

                // Spawn time-driven steps whose start time has arrived.
                for (auto& step : combo.steps)
                {
                    if (step.spawned || !step.def || !step.def->cueName.empty())
                        continue;
                    if (combo.elapsed >= step.def->startTime)
                        spawnStep(combo, step, resolveStepParent(combo, step, comboParent));
                }

                // Stop steps that have a finite duration with StopAfterDuration behavior.
                for (auto& step : combo.steps)
                {
                    if (step.childId == 0 || step.stopped || !step.def)
                        continue;
                    if (step.def->stopMode == vfx::VFXStepStopMode::StopAfterDuration && step.def->duration > 0.0f &&
                        combo.elapsed >= step.def->startTime + step.def->duration)
                    {
                        events::vfxruntime::StopVFXInstanceCommand stopCmd;
                        stopCmd.instanceId = step.childId;
                        dispatcher.execute(stopCmd);
                        step.stopped = true;
                    }
                }
            }

            // Cascade the (possibly socket-driven) transform onto every live child.
            for (auto& step : combo.steps)
            {
                if (step.childId == 0 || !step.def)
                    continue;
                events::vfxruntime::SetVFXInstanceTransformCommand xformCmd;
                xformCmd.instanceId = step.childId;
                xformCmd.worldTransform = resolveStepParent(combo, step, comboParent) * composeStepLocal(*step.def);
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
            bool allSpawned = true;
            bool anyLive = false;
            for (const auto& step : combo.steps)
            {
                if (!step.spawned)
                    allSpawned = false;
                if (step.childId != 0)
                    anyLive = true;
            }

            if (combo.playing && allSpawned && !anyLive)
                combo.playing = false;

            if (!combo.playing && allSpawned && !anyLive && combo.autoDestroyOnFinish && combo.elapsed > 0.0f)
            {
                it = combos.erase(it);
                continue;
            }
            ++it;
        }
    }

    void VFXSequenceRuntimeServiceImpl::destroyAll()
    {
        for (auto& [id, combo] : combos)
            destroyCombo(combo);
        combos.clear();
    }
}
