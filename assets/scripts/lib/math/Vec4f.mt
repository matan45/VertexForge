// Vec4f - 4D floating-point vector
// Commonly used for homogeneous coordinates and RGBA colors

public value class Vec4f {
    public float x;
    public float y;
    public float z;
    public float w;

    // Constructors
    public constructor(float x, float y, float z, float w) {
        this.x = x;
        this.y = y;
        this.z = z;
        this.w = w;
    }

    public constructor() {
        this.x = 0.0;
        this.y = 0.0;
        this.z = 0.0;
        this.w = 0.0;
    }

    // Construct from Vec3f with w component
    public static function fromVec3f(Vec3f v, float w): Vec4f {
        return new Vec4f(v.x, v.y, v.z, w);
    }

    // Vector arithmetic - return new vectors
    public function add(Vec4f other): Vec4f {
        return new Vec4f(this.x + other.x, this.y + other.y, this.z + other.z, this.w + other.w);
    }

    public function subtract(Vec4f other): Vec4f {
        return new Vec4f(this.x - other.x, this.y - other.y, this.z - other.z, this.w - other.w);
    }

    public function multiply(float scalar): Vec4f {
        return new Vec4f(this.x * scalar, this.y * scalar, this.z * scalar, this.w * scalar);
    }

    public function divide(float scalar): Vec4f {
        return new Vec4f(this.x / scalar, this.y / scalar, this.z / scalar, this.w / scalar);
    }

    public function negate(): Vec4f {
        return new Vec4f(-this.x, -this.y, -this.z, -this.w);
    }

    // Vector operations
    public function dot(Vec4f other): float {
        return this.x * other.x + this.y * other.y + this.z * other.z + this.w * other.w;
    }

    public function lengthSquared(): float {
        return this.x * this.x + this.y * this.y + this.z * this.z + this.w * this.w;
    }

    public function length(): float {
        return sqrt(this.lengthSquared());
    }

    public function normalize(): Vec4f {
        float len = this.length();
        if (len > 0.0) {
            return new Vec4f(this.x / len, this.y / len, this.z / len, this.w / len);
        }
        return new Vec4f(0.0, 0.0, 0.0, 0.0);
    }

    // Linear interpolation
    public function lerp(Vec4f other, float t): Vec4f {
        return new Vec4f(
            this.x + (other.x - this.x) * t,
            this.y + (other.y - this.y) * t,
            this.z + (other.z - this.z) * t,
            this.w + (other.w - this.w) * t
        );
    }

    // Homogeneous coordinate operations
    public function perspectiveDivide(): Vec3f {
        if (this.w != 0.0) {
            return new Vec3f(this.x / this.w, this.y / this.w, this.z / this.w);
        }
        return new Vec3f(this.x, this.y, this.z);
    }

    // Component-wise operations
    public function min(Vec4f other): Vec4f {
        float minX = this.x;
        float minY = this.y;
        float minZ = this.z;
        float minW = this.w;
        if (other.x < minX) { minX = other.x; }
        if (other.y < minY) { minY = other.y; }
        if (other.z < minZ) { minZ = other.z; }
        if (other.w < minW) { minW = other.w; }
        return new Vec4f(minX, minY, minZ, minW);
    }

    public function max(Vec4f other): Vec4f {
        float maxX = this.x;
        float maxY = this.y;
        float maxZ = this.z;
        float maxW = this.w;
        if (other.x > maxX) { maxX = other.x; }
        if (other.y > maxY) { maxY = other.y; }
        if (other.z > maxZ) { maxZ = other.z; }
        if (other.w > maxW) { maxW = other.w; }
        return new Vec4f(maxX, maxY, maxZ, maxW);
    }

    public function abs(): Vec4f {
        float absX = this.x;
        float absY = this.y;
        float absZ = this.z;
        float absW = this.w;
        if (absX < 0.0) { absX = -absX; }
        if (absY < 0.0) { absY = -absY; }
        if (absZ < 0.0) { absZ = -absZ; }
        if (absW < 0.0) { absW = -absW; }
        return new Vec4f(absX, absY, absZ, absW);
    }

    // Comparison
    public function equals(Vec4f other): bool {
        return this.x == other.x && this.y == other.y && this.z == other.z && this.w == other.w;
    }

    // Utility
    public function toString(): string {
        return "Vec4f(" + parsePrimitive(this.x) + ", " + parsePrimitive(this.y) + ", " + parsePrimitive(this.z) + ", " + parsePrimitive(this.w) + ")";
    }

    public function hashCode(): int {
        int result = hashCode(this.x);
        result = result * 31 + hashCode(this.y);
        result = result * 31 + hashCode(this.z);
        result = result * 31 + hashCode(this.w);
        return result;
    }

    // Conversion methods
    public function toVec2f(): Vec2f {
        return new Vec2f(this.x, this.y);
    }

    public function toVec3f(): Vec3f {
        return new Vec3f(this.x, this.y, this.z);
    }

    // Static factory methods
    public static function zero(): Vec4f {
        return new Vec4f(0.0, 0.0, 0.0, 0.0);
    }

    public static function one(): Vec4f {
        return new Vec4f(1.0, 1.0, 1.0, 1.0);
    }

    public static function unitX(): Vec4f {
        return new Vec4f(1.0, 0.0, 0.0, 0.0);
    }

    public static function unitY(): Vec4f {
        return new Vec4f(0.0, 1.0, 0.0, 0.0);
    }

    public static function unitZ(): Vec4f {
        return new Vec4f(0.0, 0.0, 1.0, 0.0);
    }

    public static function unitW(): Vec4f {
        return new Vec4f(0.0, 0.0, 0.0, 1.0);
    }
}
