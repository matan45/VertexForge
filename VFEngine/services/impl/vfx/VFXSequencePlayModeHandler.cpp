#include "print/Log.hpp"
#include "VFXSequencePlayModeHandler.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/animation/AnimationEventEvents.hpp"
#include "../../events/vfx/VFXSequenceRuntimeEvents.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <glm/glm.hpp>
#include <algorithm>

namespace services
{
    VFXSequencePlayModeHandler::~VFXSequencePlayModeHandler()
    {
        unsubscribeFromEvents();
    }

    void VFXSequencePlayModeHandler::subscribeToEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        editorModeChangedToken = dispatcher.subscribe<::events::editor::EditorModeChangedNotification>(
            [this](const ::events::editor::EditorModeChangedNotification& n)
            {
                onEditorModeChanged(n.previousMode, n.currentMode);
            });

        transformChangedToken = dispatcher.subscribe<::events::scene::TransformChangedNotification>(
            [this](const ::events::scene::TransformChangedNotification& n)
            {
                if (sequenceActive)
                    onTransformChanged(n.entity);
            });

        // Published on a worker/Render task — only touch the mutex-guarded queue here.
        animationEventToken = dispatcher.subscribe<::events::animation::AnimationEventFiredNotification>(
            [this](const ::events::animation::AnimationEventFiredNotification& n)
            {
                if (sequenceActive)
                    onAnimationEvent(n.entity, n.eventName);
            });
    }

    void VFXSequencePlayModeHandler::unsubscribeFromEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        if (editorModeChangedToken.isValid()) { dispatcher.unsubscribe(editorModeChangedToken); editorModeChangedToken = {}; }
        if (transformChangedToken.isValid()) { dispatcher.unsubscribe(transformChangedToken); transformChangedToken = {}; }
        if (animationEventToken.isValid()) { dispatcher.unsubscribe(animationEventToken); animationEventToken = {}; }
    }

    void VFXSequencePlayModeHandler::onEditorModeChanged(EditorMode previousMode, EditorMode currentMode)
    {
        if (previousMode == EditorMode::Edit && currentMode == EditorMode::Play)
            enterPlayMode();
        else if (previousMode == EditorMode::Play && currentMode == EditorMode::Edit)
            exitPlayMode();
    }

    void VFXSequencePlayModeHandler::onAnimationEvent(EntityHandle entity, const std::string& eventName)
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        if (pendingTriggers.size() >= MAX_PENDING_TRIGGERS)
            return; // drop under flood rather than grow unbounded
        pendingTriggers.push_back(PendingTrigger{entity, eventName});
    }

    void VFXSequencePlayModeHandler::onTransformChanged(EntityHandle entity)
    {
        auto it = autoPlayCombos.find(entity);
        if (it == autoPlayCombos.end())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto enttEntity = internal::fromHandle(entity);
        if (!registry.valid(enttEntity) || !registry.all_of<components::WorldTransformComponent>(enttEntity))
            return;

        const auto& worldTransform = registry.get<components::WorldTransformComponent>(enttEntity);

        events::vfxsequence::SetVFXComboInstanceTransformCommand cmd;
        cmd.comboId = it->second;
        cmd.worldTransform = worldTransform.worldMatrix;
        ::events::EventDispatcher::instance().execute(cmd);
    }

    void VFXSequencePlayModeHandler::drainPendingTriggers()
    {
        std::vector<PendingTrigger> triggers;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            if (pendingTriggers.empty())
                return;
            triggers.swap(pendingTriggers);
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();

        for (const auto& trigger : triggers)
        {
            auto enttEntity = internal::fromHandle(trigger.entity);
            if (!registry.valid(enttEntity) || !registry.all_of<components::VFXSequenceComponent>(enttEntity))
                continue;

            const auto& comp = registry.get<components::VFXSequenceComponent>(enttEntity);

            glm::mat4 worldMatrix{1.0f};
            if (registry.all_of<components::WorldTransformComponent>(enttEntity))
                worldMatrix = registry.get<components::WorldTransformComponent>(enttEntity).worldMatrix;

            for (const auto& mapping : comp.triggers)
            {
                if (mapping.eventName != trigger.eventName || !mapping.sequenceRef.isValid())
                    continue;

                events::vfxsequence::CreateVFXComboInstanceCommand createCmd;
                createCmd.sequenceAssetPath = mapping.sequenceRef.resolve();
                createCmd.worldTransform = worldMatrix;
                createCmd.entityId = static_cast<uint32_t>(enttEntity);
                createCmd.autoDestroyOnFinish = true;
                const VFXComboInstanceId comboId = dispatcher.execute(createCmd);
                if (comboId == 0)
                    continue;

                events::vfxsequence::PlayVFXComboInstanceCommand playCmd;
                playCmd.comboId = comboId;
                dispatcher.execute(playCmd);

                if (!mapping.socketName.empty())
                {
                    events::vfxsequence::AttachVFXComboInstanceToSocketCommand attachCmd;
                    attachCmd.comboId = comboId;
                    attachCmd.entityHandle = trigger.entity.id;
                    attachCmd.socketName = mapping.socketName;
                    dispatcher.execute(attachCmd);
                }

                triggeredCombos.push_back(comboId);
            }
        }
    }

    void VFXSequencePlayModeHandler::enterPlayMode()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();

        auto view = registry.view<components::VFXSequenceComponent, components::WorldTransformComponent>();
        for (auto entity : view)
        {
            auto& comp = view.get<components::VFXSequenceComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            if (!comp.autoPlay || !comp.sequenceRef.isValid())
                continue;

            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive)
                    continue;
            }

            events::vfxsequence::CreateVFXComboInstanceCommand createCmd;
            createCmd.sequenceAssetPath = comp.sequenceRef.resolve();
            createCmd.worldTransform = worldTransform.worldMatrix;
            createCmd.entityId = static_cast<uint32_t>(entity);
            createCmd.autoDestroyOnFinish = !comp.loop; // looping standalone combos persist until exit-play
            const VFXComboInstanceId comboId = dispatcher.execute(createCmd);
            if (comboId == 0)
                continue;

            EntityHandle handle = internal::toHandle(entity);
            autoPlayCombos[handle] = comboId;
            comp.runtimeComboId = comboId;

            events::vfxsequence::PlayVFXComboInstanceCommand playCmd;
            playCmd.comboId = comboId;
            dispatcher.execute(playCmd);

            if (!comp.socketName.empty())
            {
                events::vfxsequence::AttachVFXComboInstanceToSocketCommand attachCmd;
                attachCmd.comboId = comboId;
                attachCmd.entityHandle = handle.id;
                attachCmd.socketName = comp.socketName;
                dispatcher.execute(attachCmd);
            }
        }

        sequenceActive = true;
        vfLogInfo("VFX sequence play mode started with {} auto-played combos", autoPlayCombos.size());
    }

    void VFXSequencePlayModeHandler::exitPlayMode()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();

        auto destroy = [&dispatcher](VFXComboInstanceId comboId)
        {
            events::vfxsequence::DestroyVFXComboInstanceCommand cmd;
            cmd.comboId = comboId;
            dispatcher.execute(cmd);
        };

        for (const auto& [handle, comboId] : autoPlayCombos)
        {
            destroy(comboId);
            auto enttEntity = internal::fromHandle(handle);
            if (registry.valid(enttEntity) && registry.all_of<components::VFXSequenceComponent>(enttEntity))
                registry.get<components::VFXSequenceComponent>(enttEntity).runtimeComboId = 0;
        }
        for (VFXComboInstanceId comboId : triggeredCombos)
            destroy(comboId);

        autoPlayCombos.clear();
        triggeredCombos.clear();
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pendingTriggers.clear();
        }
        sequenceActive = false;

        vfLogInfo("VFX sequence play mode stopped");
    }

    void VFXSequencePlayModeHandler::update(float deltaTime)
    {
        if (!sequenceActive)
            return;

        drainPendingTriggers();

        auto& dispatcher = ::events::EventDispatcher::instance();
        events::vfxsequence::UpdateVFXSequenceRuntimeCommand cmd;
        cmd.deltaTime = deltaTime;
        dispatcher.execute(cmd);

        // Drop ids of triggered combos that have finished/auto-destroyed, so the list does
        // not grow across a long play session (they were only tracked for exit-play teardown).
        if (!triggeredCombos.empty())
        {
            triggeredCombos.erase(
                std::remove_if(triggeredCombos.begin(), triggeredCombos.end(),
                    [&dispatcher](VFXComboInstanceId id)
                    {
                        events::vfxsequence::IsVFXComboInstancePlayingQuery q;
                        q.comboId = id;
                        return !dispatcher.query(q);
                    }),
                triggeredCombos.end());
        }
    }
}
