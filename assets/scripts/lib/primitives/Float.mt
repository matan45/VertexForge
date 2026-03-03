// Float - Object wrapper for floating-point values
// Provides a pure OOP interface for float operations
import * from "../Object.mt";
import * from "Bool.mt";
import * from "Int.mt";

public value class Float implements Object<Float> {
    private float value;

    // Constructors
    public constructor(float val) {
        this.value = val;
    }

    public constructor() {
        this.value = 0.0;
    }

    // Accessors
    public function getValue(): float {
        return this.value;
    }

    public function setValue(float val): void {
        this.value = val;
    }

    // Arithmetic operations - return new Float objects
    public function add(Float other): Float {
        return new Float(this.value + other.value);
    }

    public function subtract(Float other): Float {
        return new Float(this.value - other.value);
    }

    public function multiply(Float other): Float {
        return new Float(this.value * other.value);
    }

    public function divide(Float other): Float {
        return new Float(this.value / other.value);
    }

    public function negate(): Float {
        return new Float(-this.value);
    }

    public function abs(): Float {
        if (this.value < 0.0) {
            return new Float(-this.value);
        }
        return new Float(this.value);
    }

    // Comparison operations
    public function equals(Float other): bool {
        return this.value == other.value;
    }

    public function compareTo(Float other): int {
        if (this.value < other.value) return -1;
        if (this.value > other.value) return 1;
        return 0;
    }

    public function lessThan(Float other): Bool {
        return new Bool(this.value < other.value);
    }

    public function lessThanOrEqual(Float other): Bool {
        return new Bool(this.value <= other.value);
    }

    public function greaterThan(Float other): Bool {
        return new Bool(this.value > other.value);
    }

    public function greaterThanOrEqual(Float other): Bool {
        return new Bool(this.value >= other.value);
    }

    // Type conversion
    public function toInt(): Int {
        // Truncates toward zero
        int intValue = 0;
        if (this.value >= 0.0) {
            intValue = this.value;  // Implicit cast truncates
        } else {
            intValue = this.value;
        }
        return new Int(intValue);
    }

    // Utility methods (Object interface)
    public function toString(): string {
        return parsePrimitive(this.value);
    }

    public function hashCode(): int {
        return hashCode(this.value);
    }

    // Static utility methods
    public static function min(Float a, Float b): Float {
        if (a.value < b.value) {
            return a;
        }
        return b;
    }

    public static function max(Float a, Float b): Float {
        if (a.value > b.value) {
            return a;
        }
        return b;
    }
}
