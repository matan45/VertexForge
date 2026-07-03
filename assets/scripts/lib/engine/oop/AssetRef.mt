// AssetRef<T> - Typed reference to an engine asset (VK-1458 OOP layer).
//
// Wraps the asset path so gameplay code passes a typed handle instead of a
// raw string. T is a PHANTOM type tag (one of the marker classes below):
// mType generics are not reified, so T never reaches the engine — it exists
// purely so `setMesh(AssetRef<MeshAsset>)` cannot take a material ref.
//
// Usage:
//   private AssetRef<MeshAsset> soldierMesh =
//       new AssetRef<MeshAsset>("assets/units/soldier.vfMesh");
//   this.gameObject().meshRenderer().setMesh(this.soldierMesh);

// Marker (phantom) types — never instantiated.
public class MeshAsset {
    public constructor() {
    }
}

public class MaterialAsset {
    public constructor() {
    }
}

public class TextureAsset {
    public constructor() {
    }
}

public class AudioAsset {
    public constructor() {
    }
}

public class AssetRef<T> {
    private string assetPath = "";

    public constructor() {
    }

    public constructor(string path) {
        this.assetPath = path;
    }

    // Project-relative asset path (forward slashes)
    public function path(): string {
        return this.assetPath;
    }

    public function isSet(): bool {
        return this.assetPath != "";
    }
}
