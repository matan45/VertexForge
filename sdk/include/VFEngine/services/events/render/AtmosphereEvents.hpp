#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "atmosphere/AtmosphereSettings.hpp"
#include <optional>

namespace events::atmosphere
{
    struct ApplyAtmosphereSettingsCommand : ICommand<>
    {
        ::render::atmosphere::AtmosphereSettings settings;

        std::string_view getName() const override { return "ApplyAtmosphereSettings"; }
    };

    // VK-1654: the same apply, but it never creates or deletes the editor's auto-created "Sun"
    // entity. Used by the MCP environment tools (they manage the sun light themselves so every
    // change is undoable) and by weather (it re-applies every frame, outside any undo entry); the
    // plain command keeps its auto-Sun behaviour for windows, scripts and scene load. A separate
    // type (instead of a flag on ApplyAtmosphereSettingsCommand) keeps that SDK-exported struct's
    // layout unchanged for plugins. Registered by the Editor and the Runtime (no auto-Sun there).
    struct ApplyAtmosphereSettingsNoSunCommand : ICommand<>
    {
        ::render::atmosphere::AtmosphereSettings settings;

        std::string_view getName() const override { return "ApplyAtmosphereSettingsNoSun"; }
    };

    // VK-1654: the directional light the renderer treats as the sun. activeOnly=true returns the
    // first effectively-active DirectionalLight with a WorldTransform (the entity the GPU light
    // buffer and the day-night sync use); activeOnly=false returns the first DirectionalLight of
    // any state (the check the auto-Sun creation uses). Registered by the Editor only.
    struct GetSunEntityQuery : IQuery<std::optional<services::EntityHandle>>
    {
        bool activeOnly = true;

        std::string_view getName() const override { return "GetSunEntity"; }
    };

    struct GetAtmosphereSettingsQuery : IQuery<::render::atmosphere::AtmosphereSettings>
    {
        std::string_view getName() const override { return "GetAtmosphereSettings"; }
    };

    struct SetAtmosphereEnabledCommand : ICommand<>
    {
        bool enabled;

        std::string_view getName() const override { return "SetAtmosphereEnabled"; }
    };

    struct GetAtmosphereEnabledQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "GetAtmosphereEnabled"; }
    };

    // VK-1566: per-frame day-night advance. Dispatched from the SunSync frame-graph step
    // (before the Transforms/worldMatrix bake). The handler advances the cycle on the live
    // atmosphere settings and, when cycleControlsSunEntity is set, rotates the sun entity.
    struct UpdateDayNightCommand : ICommand<>
    {
        float deltaTime = 0.0f;

        std::string_view getName() const override { return "UpdateDayNight"; }
    };
}
