import * from "../math/Matrix4f.mt";
import * from "VehicleWheelState.mt";

public class Vehicle {
    public constructor() {
    }

    public static function hasVehicle(int entityId): bool {
        return _native_physics_hasVehicle(entityId);
    }

    public static function createVehicle(int entityId): bool {
        return _native_physics_createVehicle(entityId);
    }

    public static function rebuildVehicle(int entityId): bool {
        return _native_physics_rebuildVehicle(entityId);
    }

    public static function destroyVehicle(int entityId): bool {
        return _native_physics_destroyVehicle(entityId);
    }

    public static function setInput(int entityId, float throttle, float steer,
                                    float brake, float handbrake): void {
        _native_physics_setVehicleInput(entityId, throttle, steer, brake, handbrake);
    }

    public static function getWheelCount(int entityId): int {
        return _native_physics_getVehicleWheelCount(entityId);
    }

    public static function getWheelState(int entityId, int wheelIndex): VehicleWheelState {
        float[] raw = _native_physics_getVehicleWheelState(entityId, wheelIndex);
        if (raw[0] < 0.5) {
            return new VehicleWheelState();
        }

        Matrix4f transform = new Matrix4f(
            raw[6], raw[7], raw[8], raw[9],
            raw[10], raw[11], raw[12], raw[13],
            raw[14], raw[15], raw[16], raw[17],
            raw[18], raw[19], raw[20], raw[21]
        );

        return new VehicleWheelState(
            true,
            raw[1],
            raw[2],
            raw[3],
            raw[4],
            raw[5] > 0.5,
            transform
        );
    }

    public static function getWheelTransform(int entityId, int wheelIndex): Matrix4f {
        VehicleWheelState state = Vehicle::getWheelState(entityId, wheelIndex);
        return state.worldTransform;
    }
}
