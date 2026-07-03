// NavAgent - Typed wrapper over the entity's navmesh agent.
// Stateless view forwarding 1:1 to the Navmesh static facade.
//
// Usage:
//   NavAgent agent = this.gameObject().navAgent();
//   agent.setDestination(target.transform().worldPosition());

import * from "../../math/Vec3f.mt";
import * from "../Navmesh.mt";
import * from "../Entity.mt";
import * from "../ComponentType.mt";
import * from "Component.mt";

public class NavAgent extends Component {
    public constructor(int entityId) : super(entityId) {
    }

    @Override
    public function exists(): bool {
        return Entity::hasComponent(this.entityId, ComponentType::NAVMESH_AGENT);
    }

    // --- movement ---

    public function setDestination(Vec3f target): void {
        Navmesh::setDestination(this.entityId, target);
    }

    public function moveTo(Vec3f target): void {
        Navmesh::moveTo(this.entityId, target);
    }

    public function stop(): void {
        Navmesh::stopAgent(this.entityId);
    }

    // --- tuning ---

    public function setSpeed(float speed): void {
        Navmesh::setSpeed(this.entityId, speed);
    }

    public function speed(): float {
        return Navmesh::getSpeed(this.entityId);
    }

    public function setAcceleration(float accel): void {
        Navmesh::setAcceleration(this.entityId, accel);
    }

    public function setTurnSpeed(float degPerSec): void {
        Navmesh::setTurnSpeed(this.entityId, degPerSec);
    }

    public function velocity(): Vec3f {
        return Navmesh::getVelocity(this.entityId);
    }

    // --- root motion pacing ---

    public function setRootMotionDriven(bool enabled): void {
        Navmesh::setRootMotionDriven(this.entityId, enabled);
    }

    public function setRootMotionScale(float scale): void {
        Navmesh::setRootMotionScale(this.entityId, scale);
    }
}
