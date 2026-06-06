#pragma once

// RTS gameplay tag components (VK-1302). Registered as plugin components so the
// engine core stays game-agnostic — scripts access them via PluginComponent.mt
// (has/getInt/add/remove/findAll) and designers via the Add Component popup.

// Marks an entity as selectable by the RTS selection system.
struct SelectableComponent
{
    bool canBeSelected = true;
};

// Runtime-only selection marker managed by the selection controller during play.
// Never authored in scenes (scenes are saved in edit mode, where it is absent).
struct SelectedComponent
{
    bool active = true;
};

// Faction ownership: 0 = Player, 1 = Enemy, 2 = Neutral.
struct TeamComponent
{
    int teamId = 0;
};

// Fog-of-war vision source (VK-1314): the entity reveals a circle of sightRadius
// world units around itself for its team. Author on player units and buildings.
struct VisionComponent
{
    float sightRadius = 24.0f;
};
