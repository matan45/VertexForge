// Vec3f - 3D floating-point vector
// Provides common 3D vector operations for graphics and math

import * from "Vec2f.mt";

public value class Vec3f {
    public float x;
    public float y;
    public float z;

    // Constructors
    public constructor(float x, float y, float z) {
        this.x = x;
        this.y = y;
        this.z = z;
    }

    public constructor() {
        this.x = 0.0;
        this.y = 0.0;
        this.z = 0.0;
    }

    // Vector arithmetic - return new vectors
    public function add(Vec3f other): Vec3f {
        return new Vec3f(this.x + other.x, this.y + other.y, this.z + other.z);
    }

    public function subtract(Vec3f other): Vec3f {
        return new Vec3f(this.x - other.x, this.y - other.y, this.z - other.z);
    }

    public function multiply(float scalar): Vec3f {
        return new Vec3f(this.x * scalar, this.y * scalar, this.z * scalar);
    }

    public function divide(float scalar): Vec3f {
        return new Vec3f(this.x / scalar, this.y / scalar, this.z / scalar);
    }

    public function negate(): Vec3f {
        return new Vec3f(-this.x, -this.y, -this.z);
    }

    // Vector operations
    public function dot(Vec3f other): float {
        return this.x * other.x + this.y * other.y + this.z * other.z;
    }

    public function cross(Vec3f other): Vec3f {
        return new Vec3f(
            this.y * other.z - this.z * other.y,
            this.z * other.x - this.x * other.z,
            this.x * other.y - this.y * other.x
        );
    }

    public function lengthSquared(): float {
        return this.x * this.x + this.y * this.y + this.z * this.z;
    }

    public function length(): float {
        return sqrt(this.lengthSquared());
    }

    public function normalize(): Vec3f {
        float len = this.length();
        if (len > 0.0) {
            return new Vec3f(this.x / len, this.y / len, this.z / len);
        }
        return new Vec3f(0.0, 0.0, 0.0);
    }

    public function distance(Vec3f other): float {
        return this.subtract(other).length();
    }

    public function distanceSquared(Vec3f other): float {
        return this.subtract(other).lengthSquared();
    }

    // Linear interpolation
    public function lerp(Vec3f other, float t): Vec3f {
        return new Vec3f(
            this.x + (other.x - this.x) * t,
            this.y + (other.y - this.y) * t,
            this.z + (other.z - this.z) * t
        );
    }

    // Reflect vector around a normal
    public function reflect(Vec3f normal): Vec3f {
        float d = 2.0 * this.dot(normal);
        return new Vec3f(
            this.x - normal.x * d,
            this.y - normal.y * d,
            this.z - normal.z * d
        );
    }

    // Project this vector onto another vector
    public function project(Vec3f onto): Vec3f {
        float d = onto.dot(onto);
        if (d > 0.0) {
            float s = this.dot(onto) / d;
            return onto.multiply(s);
        }
        return new Vec3f(0.0, 0.0, 0.0);
    }

    // Component-wise operations
    public function min(Vec3f other): Vec3f {
        float minX = this.x;
        float minY = this.y;
        float minZ = this.z;
        if (other.x < minX) { minX = other.x; }
        if (other.y < minY) { minY = other.y; }
        if (other.z < minZ) { minZ = other.z; }
        return new Vec3f(minX, minY, minZ);
    }

    public function max(Vec3f other): Vec3f {
        float maxX = this.x;
        float maxY = this.y;
        float maxZ = this.z;
        if (other.x > maxX) { maxX = other.x; }
        if (other.y > maxY) { maxY = other.y; }
        if (other.z > maxZ) { maxZ = other.z; }
        return new Vec3f(maxX, maxY, maxZ);
    }

    public function abs(): Vec3f {
        float absX = this.x;
        float absY = this.y;
        float absZ = this.z;
        if (absX < 0.0) { absX = -absX; }
        if (absY < 0.0) { absY = -absY; }
        if (absZ < 0.0) { absZ = -absZ; }
        return new Vec3f(absX, absY, absZ);
    }

    // Comparison
    public function equals(Vec3f other): bool {
        return this.x == other.x && this.y == other.y && this.z == other.z;
    }

    // Utility
    public function toString(): string {
        return "Vec3f(" + parsePrimitive(this.x) + ", " + parsePrimitive(this.y) + ", " + parsePrimitive(this.z) + ")";
    }

    public function hashCode(): int {
        int result = hashCode(this.x);
        result = result * 31 + hashCode(this.y);
        result = result * 31 + hashCode(this.z);
        return result;
    }

    // Conversion to Vec2f (drops z)
    public function toVec2f(): Vec2f {
        return new Vec2f(this.x, this.y);
    }

    // Static factory methods
    public static function zero(): Vec3f {
        return new Vec3f(0.0, 0.0, 0.0);
    }

    public static function one(): Vec3f {
        return new Vec3f(1.0, 1.0, 1.0);
    }

    public static function unitX(): Vec3f {
        return new Vec3f(1.0, 0.0, 0.0);
    }

    public static function unitY(): Vec3f {
        return new Vec3f(0.0, 1.0, 0.0);
    }

    public static function unitZ(): Vec3f {
        return new Vec3f(0.0, 0.0, 1.0);
    }

    public static function up(): Vec3f {
        return new Vec3f(0.0, 1.0, 0.0);
    }

    public static function down(): Vec3f {
        return new Vec3f(0.0, -1.0, 0.0);
    }

    public static function left(): Vec3f {
        return new Vec3f(-1.0, 0.0, 0.0);
    }

    public static function right(): Vec3f {
        return new Vec3f(1.0, 0.0, 0.0);
    }

    public static function forward(): Vec3f {
        return new Vec3f(0.0, 0.0, -1.0);
    }

    public static function back(): Vec3f {
        return new Vec3f(0.0, 0.0, 1.0);
    }
}
