// CameraComponent - Typed wrapper over the entity's camera.
// Stateless view forwarding 1:1 to the Camera static facade. Named
// CameraComponent because the static facade already owns the Camera name
// (mType has no namespaces).

import * from "../../math/Vec3f.mt";
import * from "../../math/Matrix4f.mt";
import * from "../Camera.mt";
import * from "../Entity.mt";
import * from "../ComponentType.mt";
import * from "Component.mt";

public class CameraComponent extends Component {
    public constructor(int entityId) : super(entityId) {
    }

    @Override
    public function exists(): bool {
        return Entity::hasComponent(this.entityId, ComponentType::CAMERA);
    }

    // --- projection ---

    public function fov(): float {
        return Camera::getFOV(this.entityId);
    }

    public function setFov(float fov): void {
        Camera::setFOV(this.entityId, fov);
    }

    public function nearPlane(): float {
        return Camera::getNearPlane(this.entityId);
    }

    public function setNearPlane(float near): void {
        Camera::setNearPlane(this.entityId, near);
    }

    public function farPlane(): float {
        return Camera::getFarPlane(this.entityId);
    }

    public function setFarPlane(float far): void {
        Camera::setFarPlane(this.entityId, far);
    }

    public function isOrthographic(): bool {
        return Camera::isOrthographic(this.entityId);
    }

    public function setOrthographic(bool orthographic): void {
        Camera::setOrthographic(this.entityId, orthographic);
    }

    public function orthoSize(): float {
        return Camera::getOrthoSize(this.entityId);
    }

    public function setOrthoSize(float size): void {
        Camera::setOrthoSize(this.entityId, size);
    }

    // --- primary + culling ---

    public function isPrimary(): bool {
        return Camera::getPrimary() == this.entityId;
    }

    public function setPrimary(bool isPrimary): void {
        Camera::setIsPrimary(this.entityId, isPrimary);
    }

    public function cullingMask(): int {
        return Camera::getCullingMask(this.entityId);
    }

    public function setCullingMask(int mask): void {
        Camera::setCullingMask(this.entityId, mask);
    }

    // --- view ---

    public function lookAt(Vec3f target, Vec3f up): void {
        Camera::lookAt(this.entityId, target, up);
    }

    public function viewMatrix(): Matrix4f {
        return Camera::getViewMatrix(this.entityId);
    }

    public function projectionMatrix(): Matrix4f {
        return Camera::getProjectionMatrix(this.entityId);
    }
}
