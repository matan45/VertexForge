#pragma once

#include "asset/AssetRef.hpp"
#include <glm/glm.hpp>

// RTS gameplay tag components (VK-1302). Registered as plugin components so the
// engine core stays game-agnostic — scripts access them via PluginComponent.mt
// (has/getInt/add/remove/findAll) and designers via the Add Component popup.

// Marks an entity as selectable by the RTS selection system.
struct SelectableComponent
{
    bool canBeSelected = true;
    // Portrait icon shown in the selection panel. Authored via drag-drop in the
    // inspector (engine resolves the GUID; the plugin only declares the field).
    asset::AssetRef portrait;
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
    // Faction tint used for minimap blips / unit highlight (RGBA 0..1).
    glm::vec4 color{0.2f, 0.55f, 1.0f, 1.0f};
};

// Fog-of-war vision source (VK-1314): the entity reveals a circle of sightRadius
// world units around itself for its team. Author on player units and buildings.
struct VisionComponent
{
    float sightRadius = 24.0f;
};

// Hit points (VK-1404): authored on combat units and buildings. Damage is applied
// via the _rts_apply_damage native (see CombatRules.hpp); scripts react to the
// "rts.unit_killed" event for death/cleanup rather than the native destroying it.
struct HealthComponent
{
    float maxHP = 100.0f;
    float currentHP = 100.0f;
};

// Attack profile (VK-1404): damage per hit, engagement range in world units, and
// the cooldown between hits. lastAttackTime is scratch state the combat script
// stamps to throttle attacks; it is not authored.
struct AttackComponent
{
    float damage = 10.0f;
    float range = 8.0f;
    float cooldown = 1.0f;
    float lastAttackTime = 0.0f;
};
