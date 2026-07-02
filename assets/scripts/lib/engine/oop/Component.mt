// Component - Base class for typed component wrappers (VK-1458 OOP layer).
//
// A wrapper is a STATELESS VIEW over an entity id: it caches nothing about
// the component itself, every method forwards to the existing static facades
// (Physics, Camera, Animator, ...), and the public entityId is the deliberate
// escape hatch for dropping down to the static API.
//
// Accessors on GameObject return wrappers unconditionally (never null); call
// exists() once when the component may be absent — the underlying natives
// tolerate missing components the same way the static API always has.

public class Component {
    public final int entityId;

    public constructor(int entityId) {
        this.entityId = entityId;
    }

    // True when the wrapped component is actually present on the entity.
    // Subclasses override with their facade's has* query.
    public function exists(): bool {
        return false;
    }
}
