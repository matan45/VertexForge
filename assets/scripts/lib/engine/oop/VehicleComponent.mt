import * from "../ComponentType.mt";
import * from "../Entity.mt";
import * from "../Vehicle.mt";
import * from "../VehicleWheelState.mt";
import * from "../../math/Matrix4f.mt";
import * from "Component.mt";

public class VehicleComponent extends Component {
    public constructor(int entityId) : super(entityId) {
    }

    @Override
    public function exists(): bool {
        return Entity::hasComponent(this.entityId, ComponentType::VEHICLE);
    }

    public function isActive(): bool {
        return Vehicle::hasVehicle(this.entityId);
    }

    public function createVehicle(): bool {
        return Vehicle::createVehicle(this.entityId);
    }

    public function rebuildVehicle(): bool {
        return Vehicle::rebuildVehicle(this.entityId);
    }

    public function destroyVehicle(): bool {
        return Vehicle::destroyVehicle(this.entityId);
    }

    public function setInput(float throttle, float steer, float brake, float handbrake): void {
        Vehicle::setInput(this.entityId, throttle, steer, brake, handbrake);
    }

    public function wheelCount(): int {
        return Vehicle::getWheelCount(this.entityId);
    }

    public function wheelState(int wheelIndex): VehicleWheelState {
        return Vehicle::getWheelState(this.entityId, wheelIndex);
    }

    public function wheelTransform(int wheelIndex): Matrix4f {
        return Vehicle::getWheelTransform(this.entityId, wheelIndex);
    }
}
