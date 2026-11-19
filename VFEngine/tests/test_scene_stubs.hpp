#pragma once

#include "events/EventDispatcher.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/render/PostProcessEvents.hpp"
#include "events/render/AtmosphereEvents.hpp"
#include "events/render/CloudEvents.hpp"
#include "events/physics/PhysicsSettingsEvents.hpp"
#include "events/audio/AudioSettingsEvents.hpp"
#include "events/terrain/OceanEvents.hpp"
#include <string>
#include <type_traits>

// No-op handlers for the renderer / physics / audio / ocean commands that ScenePersistenceService
// issues without a guard, so a CPU-only test can run newScene, a disk load (update()) and a snapshot
// restore. EventDispatcher::execute throws when a command has no handler.
namespace test_scene_stubs
{
    template <typename TCommand>
    void stubCommand()
    {
        events::EventDispatcher::instance().registerCommandHandler<TCommand>(
            [](const TCommand&) -> typename TCommand::ResultType
            {
                if constexpr (std::is_void_v<typename TCommand::ResultType>)
                    return;
                else
                    return typename TCommand::ResultType{};
            });
    }

    // The commands newScene, a disk load and a snapshot restore issue.
    inline void stubSceneLoadCommands()
    {
        stubCommand<events::render::RemoveIBLCommand>();
        stubCommand<events::render::SetIBLParamsCommand>();
        stubCommand<events::render::ApplyShadowSettingsCommand>();
        stubCommand<events::ocean::RebuildOceanFromComponentsCommand>();
        stubCommand<events::physics::ApplyPhysicsSettingsCommand>();
        stubCommand<events::audio::ApplyAudioSettingsCommand>();
        stubCommand<events::postprocess::ApplyPostProcessSettingsCommand>();
        stubCommand<events::atmosphere::ApplyAtmosphereSettingsCommand>();
        stubCommand<events::cloud::ApplyCloudSettingsCommand>();
        events::EventDispatcher::instance().registerQueryHandler<events::scene::GetCurrentScenePathQuery>(
            [](const events::scene::GetCurrentScenePathQuery&) { return std::string{}; });
    }
}
