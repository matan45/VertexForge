// ComboCuePayload - typed payload delivered to IVFXComboCueListener.onComboCue (VK-1495)
//
// The source fields (position / color / scalar) are all optional at the sequence
// layer, so each carries a has* flag: a "false" flag means the cue author did not
// set that field, which is distinct from a real zero value. Always check the flag
// before using a field.
//
// Usage:
//   @Override
//   public function onComboCue(int comboId, string cueName, ComboCuePayload p): void {
//       if (p.hasScalar) { Camera::shake(p.scalar); }
//       if (p.hasPosition) { spawnHit(p.position()); }
//   }

import * from "Vec3f.mt";

public class ComboCuePayload {
    public bool hasPosition = false;
    public float posX = 0.0;
    public float posY = 0.0;
    public float posZ = 0.0;

    public bool hasColor = false;
    public float colorR = 0.0;
    public float colorG = 0.0;
    public float colorB = 0.0;
    public float colorA = 0.0;

    public bool hasScalar = false;
    public float scalar = 0.0;

    public constructor() {
    }

    public constructor(bool hasPosition, float posX, float posY, float posZ,
                        bool hasColor, float colorR, float colorG, float colorB, float colorA,
                        bool hasScalar, float scalar) {
        this.hasPosition = hasPosition;
        this.posX = posX;
        this.posY = posY;
        this.posZ = posZ;
        this.hasColor = hasColor;
        this.colorR = colorR;
        this.colorG = colorG;
        this.colorB = colorB;
        this.colorA = colorA;
        this.hasScalar = hasScalar;
        this.scalar = scalar;
    }

    // World-space cue position. Check hasPosition first.
    public function position(): Vec3f {
        return new Vec3f(this.posX, this.posY, this.posZ);
    }
}
