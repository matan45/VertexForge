// Vec2f - 2D floating-point vector
// Provides common 2D vector operations for graphics and math

public value class Vec2f {
    public float x;
    public float y;

    // Constructors
    public constructor(float x, float y) {
        this.x = x;
        this.y = y;
    }

    public constructor() {
        this.x = 0.0;
        this.y = 0.0;
    }

    // Vector arithmetic - return new vectors
    public function add(Vec2f other): Vec2f {
        return new Vec2f(this.x + other.x, this.y + other.y);
    }

    public function subtract(Vec2f other): Vec2f {
        return new Vec2f(this.x - other.x, this.y - other.y);
    }

    public function multiply(float scalar): Vec2f {
        return new Vec2f(this.x * scalar, this.y * scalar);
    }

    public function divide(float scalar): Vec2f {
        return new Vec2f(this.x / scalar, this.y / scalar);
    }

    public function negate(): Vec2f {
        return new Vec2f(-this.x, -this.y);
    }

    // Vector operations
    public function dot(Vec2f other): float {
        return this.x * other.x + this.y * other.y;
    }

    public function lengthSquared(): float {
        return this.x * this.x + this.y * this.y;
    }

    public function length(): float {
        return sqrt(this.lengthSquared());
    }

    public function normalize(): Vec2f {
        float len = this.length();
        if (len > 0.0) {
            return new Vec2f(this.x / len, this.y / len);
        }
        return new Vec2f(0.0, 0.0);
    }

    public function distance(Vec2f other): float {
        return this.subtract(other).length();
    }

    public function distanceSquared(Vec2f other): float {
        return this.subtract(other).lengthSquared();
    }

    // Linear interpolation
    public function lerp(Vec2f other, float t): Vec2f {
        return new Vec2f(
            this.x + (other.x - this.x) * t,
            this.y + (other.y - this.y) * t
        );
    }

    // Perpendicular vector (rotated 90 degrees counter-clockwise)
    public function perpendicular(): Vec2f {
        return new Vec2f(-this.y, this.x);
    }

    // Angle in radians from positive x-axis
    public function angle(): float {
        return atan2(this.y, this.x);
    }

    // Rotate by angle in radians
    public function rotate(float radians): Vec2f {
        float c = cos(radians);
        float s = sin(radians);
        return new Vec2f(
            this.x * c - this.y * s,
            this.x * s + this.y * c
        );
    }

    // Component-wise operations
    public function min(Vec2f other): Vec2f {
        float minX = this.x;
        float minY = this.y;
        if (other.x < minX) { minX = other.x; }
        if (other.y < minY) { minY = other.y; }
        return new Vec2f(minX, minY);
    }

    public function max(Vec2f other): Vec2f {
        float maxX = this.x;
        float maxY = this.y;
        if (other.x > maxX) { maxX = other.x; }
        if (other.y > maxY) { maxY = other.y; }
        return new Vec2f(maxX, maxY);
    }

    public function abs(): Vec2f {
        float absX = this.x;
        float absY = this.y;
        if (absX < 0.0) { absX = -absX; }
        if (absY < 0.0) { absY = -absY; }
        return new Vec2f(absX, absY);
    }

    // Comparison
    public function equals(Vec2f other): bool {
        return this.x == other.x && this.y == other.y;
    }

    // Utility
    public function toString(): string {
        return "Vec2f(" + parsePrimitive(this.x) + ", " + parsePrimitive(this.y) + ")";
    }

    public function hashCode(): int {
        return hashCode(this.x) * 31 + hashCode(this.y);
    }

    // Static factory methods
    public static function zero(): Vec2f {
        return new Vec2f(0.0, 0.0);
    }

    public static function one(): Vec2f {
        return new Vec2f(1.0, 1.0);
    }

    public static function unitX(): Vec2f {
        return new Vec2f(1.0, 0.0);
    }

    public static function unitY(): Vec2f {
        return new Vec2f(0.0, 1.0);
    }

    public static function fromAngle(float radians): Vec2f {
        return new Vec2f(cos(radians), sin(radians));
    }
}
