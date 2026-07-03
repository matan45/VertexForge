// MeshRenderer - Typed wrapper over the entity's mesh + material rendering.
// Stateless view forwarding to the Entity/Material static facades.

import * from "../../math/Vec2f.mt";
import * from "../../math/Vec3f.mt";
import * from "../Entity.mt";
import * from "../Material.mt";
import * from "../ComponentType.mt";
import * from "Component.mt";
import * from "AssetRef.mt";

public class MeshRenderer extends Component {
    public constructor(int entityId) : super(entityId) {
    }

    @Override
    public function exists(): bool {
        return Entity::hasComponent(this.entityId, ComponentType::MESH);
    }

    // --- mesh / material assets ---

    public function setMesh(AssetRef<MeshAsset> mesh): bool {
        return Entity::setMesh(this.entityId, mesh.path());
    }

    public function setMesh(string meshPath): bool {
        return Entity::setMesh(this.entityId, meshPath);
    }

    public function setMaterial(AssetRef<MaterialAsset> material): bool {
        return Entity::setMaterial(this.entityId, material.path());
    }

    public function setMaterial(string materialPath): bool {
        return Entity::setMaterial(this.entityId, materialPath);
    }

    // --- render layer (camera culling-mask bit, 0-31) ---

    public function setRenderLayer(int layer): bool {
        return Entity::setRenderLayer(this.entityId, layer);
    }

    public function renderLayer(): int {
        return Entity::getRenderLayer(this.entityId);
    }

    // --- per-entity material parameter overrides ---

    public function setScalar(string name, float value): bool {
        return Material::setScalar(this.entityId, name, value);
    }

    public function setVec2(string name, Vec2f value): bool {
        return Material::setVec2(this.entityId, name, value);
    }

    public function setVec3(string name, Vec3f value): bool {
        return Material::setVec3(this.entityId, name, value);
    }

    public function setColor(string name, float r, float g, float b, float a): bool {
        return Material::setColor(this.entityId, name, r, g, b, a);
    }

    public function getScalar(string name): float {
        return Material::getScalar(this.entityId, name);
    }

    public function hasParameter(string name): bool {
        return Material::hasParameter(this.entityId, name);
    }

    public function resetParameter(string name): bool {
        return Material::resetParameter(this.entityId, name);
    }

    public function resetAllParameters(): bool {
        return Material::resetAllParameters(this.entityId);
    }
}
