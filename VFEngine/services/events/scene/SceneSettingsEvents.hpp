#pragma once
#include "../EventTypes.hpp"
#include <cstdint>

namespace events::scene
{
    // VK-1654: which part of the scene's render settings changed. The settings themselves live in
    // types::RenderSettings (Get/SetRenderSettings in ComponentPhysicsLightEvents.hpp); IBL is the
    // scene root's IBLComponent. VK-1655 adds PostProcess (every post-process effect except the
    // volumetric fog), Shadows and Display (MSAA).
    enum class RenderSettingsSection : uint32_t
    {
        All,
        Atmosphere,
        Cloud,
        VolumetricFog,
        GI,
        IBL,
        PostProcess,
        Shadows,
        Display
    };

    // Published after an out-of-window edit of the scene's render settings (MCP tools and their
    // undo/redo) so editor windows that cache a copy reload it, and after a scene load / new scene
    // (sceneLoad=true) so services that are not re-applied by ScenePersistenceService (GI, which is
    // registered by the Editor only) apply the loaded values.
    struct RenderSettingsChangedNotification : INotification
    {
        RenderSettingsSection section = RenderSettingsSection::All;
        bool sceneLoad = false;

        std::string_view getName() const override { return "RenderSettingsChanged"; }
    };

    // True while a scene load (or a Play->Stop snapshot restore) is queued or still streaming in.
    // An edit made in that window would land on the outgoing scene and be lost.
    struct IsSceneLoadPendingQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "IsSceneLoadPending"; }
    };
}
