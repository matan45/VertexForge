// Int - Object wrapper for integer values
// Provides a pure OOP interface for integer operations
import * from "../Object.mt";
import * from "./Bool.mt";

public value class Int implements Object<Int> {
    private int value;

    // Constructors
    public constructor(int val) {
        this.value = val;
    }

    public constructor() {
        this.value = 0;
    }

    // Accessors
    public function getValue(): int {
        return this.value;
    }

    public function setValue(int val): void {
        this.value = val;
    }

    // Arithmetic operations - return new Int objects
    public function add(Int other): Int {
        return new Int(this.value + other.value);
    }

    public function subtract(Int other): Int {
        return new Int(this.value - other.value);
    }

    public function multiply(Int other): Int {
        return new Int(this.value * other.value);
    }

    public function divide(Int other): Int {
        return new Int(this.value / other.value);
    }

    public function modulo(Int other): Int {
        return new Int(this.value % other.value);
    }

    public function negate(): Int {
        return new Int(-this.value);
    }

    public function abs(): Int {
        if (this.value < 0) {
            return new Int(-this.value);
        }
        return new Int(this.value);
    }

    // Comparison operations
    public function equals(Int other): bool {
        return this.value == other.value;
    }

    public function compareTo(Int other): int {
        if (this.value < other.value) return -1;
        if (this.value > other.value) return 1;
        return 0;
    }

    public function lessThan(Int other): Bool {
        return new Bool(this.value < other.value);
    }

    public function lessThanOrEqual(Int other): Bool {
        return new Bool(this.value <= other.value);
    }

    public function greaterThan(Int other): Bool {
        return new Bool(this.value > other.value);
    }

    public function greaterThanOrEqual(Int other): Bool {
        return new Bool(this.value >= other.value);
    }

    // Utility methods (Object interface)
    public function toString(): string {
        return parsePrimitive(this.value);
    }

    public function hashCode(): int {
        return hashCode(this.value);
    }

    // Static utility methods
    public static function min(Int a, Int b): Int {
        if (a.value < b.value) {
            return a;
        }
        return b;
    }

    public static function max(Int a, Int b): Int {
        if (a.value > b.value) {
            return a;
        }
        return b;
    }
}
