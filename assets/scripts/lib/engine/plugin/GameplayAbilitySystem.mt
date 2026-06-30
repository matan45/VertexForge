// GameplayAbilitySystem - mType facade over the GAS plugin natives (VK-816).
//
// The supported scripting entry point for the Gameplay Ability System. Scripts
// should call these methods rather than mutating GAS_* plugin components
// directly (generic PluginComponent access is debug/tooling only). Every method
// forwards to a _gas_* native registered by the GameplayAbilitySystem plugin;
// the natives return handles/status codes and NEVER destroy entities (death and
// cleanup are decided by scripts reacting to gameplay, not by the natives).
//
// Activation status codes (returned by canActivate / activate):
//   0 Success | 1 InvalidEntity | 2 NotGranted | 3 BlockedByTags
//   4 MissingTags | 5 OnCooldown | 6 CostNotMet | 7 NoTarget | 8 Internal
//
// Usage:
//   int self = Entity::self();
//   GameplayAbilitySystem::grantAbility(self, "gas/abilities/fireball.vfAbility");
//   if (GameplayAbilitySystem::isSuccess(GameplayAbilitySystem::canActivate(self, "Fireball", target))) {
//       GameplayAbilitySystem::activate(self, "Fireball", target);
//   }
//   float hp = GameplayAbilitySystem::getAttribute(self, "Health");
//   if (GameplayAbilitySystem::hasTag(self, "Character.Stunned")) { /* ... */ }

public class GameplayAbilitySystem {
    public constructor() {
    }

    // No-explicit-target sentinel (Self-targeted abilities ignore the target).
    public static function noTarget(): int {
        return -1;
    }

    // True when an activation status code means success.
    public static function isSuccess(int status): bool {
        return status == 0;
    }

    // ============================================
    // Abilities
    // ============================================

    // Grant an ability to an entity by .vfAbility asset path. Returns 1 on
    // success, 0 if the asset could not be loaded.
    public static function grantAbility(int entityId, string abilityPath): int {
        return _gas_grant_ability(entityId, abilityPath);
    }

    // Revoke a previously-granted ability by its ability id. Returns 1 if it was
    // granted (and is now removed), 0 otherwise.
    public static function revokeAbility(int entityId, string abilityId): int {
        return _gas_revoke_ability(entityId, abilityId);
    }

    // Check whether an ability can be activated right now. Returns a status code
    // (0 == Success). Does not mutate anything.
    public static function canActivate(int entityId, string abilityId, int targetId): int {
        return _gas_can_activate(entityId, abilityId, targetId);
    }

    public static function canActivateSelf(int entityId, string abilityId): int {
        return _gas_can_activate(entityId, abilityId, -1);
    }

    // Activate an ability against a target (use noTarget() for Self abilities).
    // Transactional: returns a status code; on failure nothing is mutated.
    public static function activate(int entityId, string abilityId, int targetId): int {
        return _gas_activate(entityId, abilityId, targetId);
    }

    public static function activateSelf(int entityId, string abilityId): int {
        return _gas_activate(entityId, abilityId, -1);
    }

    // Set the transient target used by input-polled OnPressed abilities.
    // Use noTarget() to clear it. Direct activate(...) calls can still pass an
    // explicit target and do not depend on this value.
    public static function setTarget(int entityId, int targetId): int {
        return _gas_set_target(entityId, targetId);
    }

    // Cancel an in-flight activation by its handle (no-op if not active).
    public static function cancel(int entityId, int activationHandle): void {
        _gas_cancel(entityId, activationHandle);
    }

    public static function getCooldownRemaining(int entityId, string abilityId): float {
        return _gas_get_cooldown_remaining(entityId, abilityId);
    }

    // ============================================
    // Effects
    // ============================================

    // Apply a gameplay effect (.vfGameplayEffect path) from source to target.
    // Returns the active-effect handle (> 0) or 0 for an Instant effect / failure.
    public static function applyEffect(int sourceId, int targetId, string effectPath): int {
        return _gas_apply_effect(sourceId, targetId, effectPath);
    }

    // Remove an active effect by handle. Returns true if it was present.
    public static function removeEffect(int targetId, int effectHandle): bool {
        return _gas_remove_effect(targetId, effectHandle);
    }

    // ============================================
    // Attributes
    // ============================================

    // Current (aggregated) value of a named attribute, or 0 if absent.
    public static function getAttribute(int entityId, string name): float {
        return _gas_get_attribute(entityId, name);
    }

    // Base value of a named attribute (before continuous modifiers), or 0.
    public static function getAttributeBase(int entityId, string name): float {
        return _gas_get_attribute_base(entityId, name);
    }

    // ============================================
    // Gameplay Tags (hierarchical, dotted — "Character.Stunned")
    // ============================================

    // True if the entity owns this tag or a hierarchical descendant of it.
    public static function hasTag(int entityId, string tag): bool {
        return _gas_has_tag(entityId, tag);
    }

    public static function hasAllTags(int entityId, string[] tags): bool {
        return _gas_has_all_tags(entityId, tags);
    }

    public static function hasAnyTag(int entityId, string[] tags): bool {
        return _gas_has_any_tags(entityId, tags);
    }

    // All tags currently on the entity (loose + effect-granted + ability-owned).
    public static function getTags(int entityId): string[] {
        return _gas_get_tags(entityId);
    }
}
