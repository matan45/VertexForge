// PrefabRef - Typed reference to a .vfPrefab (VK-1458 OOP layer).
//
// Holds the prefab path once, at the declaration site; call sites stay
// string-free and get GameObject results instead of raw ids. Set the path
// via the field initializer today; a future inspector can write fields
// tagged @SerializeField instead.
//
// Usage:
//   private PrefabRef bulletPrefab = new PrefabRef("assets/prefabs/bullet.vfPrefab");
//
//   GameObject? bullet = this.bulletPrefab.instantiateAt(muzzle);
//   if (bullet != null) { bullet.rigidBody().applyImpulse(dir.multiply(40.0)); }

import * from "../../math/Vec3f.mt";
import * from "../Entity.mt";
import * from "GameObject.mt";

public class PrefabRef {
    private string prefabPath = "";

    public constructor() {
    }

    public constructor(string path) {
        this.prefabPath = path;
    }

    // Project-relative .vfPrefab path (forward slashes)
    public function path(): string {
        return this.prefabPath;
    }

    public function isSet(): bool {
        return this.prefabPath != "";
    }

    // Instantiate at the scene root. Null when the prefab failed to load.
    public function instantiate(): GameObject? {
        int rootId = Entity::instantiate(this.prefabPath);
        if (rootId < 0) {
            return null;
        }
        return new GameObject(rootId);
    }

    // Instantiate at the scene root, then place the root at a local position.
    public function instantiateAt(Vec3f position): GameObject? {
        GameObject? instance = this.instantiate();
        if (instance != null) {
            instance.transform().setLocalPosition(position);
        }
        return instance;
    }

    // Instantiate parented under the given GameObject.
    public function instantiateUnder(GameObject parent): GameObject? {
        int rootId = Entity::instantiateChild(this.prefabPath, parent.id);
        if (rootId < 0) {
            return null;
        }
        return new GameObject(rootId);
    }
}
