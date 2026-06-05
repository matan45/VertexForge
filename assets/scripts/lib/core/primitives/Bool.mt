// Bool - Object wrapper for boolean values
// Provides a pure OOP interface for boolean operations
public value class Bool {
    private bool value;

    // Constructors
    public constructor(bool val) {
        this.value = val;
    }

    public constructor() {
        this.value = false;
    }

    // Accessors
    public function getValue(): bool {
        return this.value;
    }

    public function setValue(bool val): void {
        this.value = val;
    }

    // Logical operations - return new Bool objects
    public function and(Bool other): Bool {
        return new Bool(this.value && other.value);
    }

    public function or(Bool other): Bool {
        return new Bool(this.value || other.value);
    }

    public function xor(Bool other): Bool {
        bool result = (this.value || other.value) && !(this.value && other.value);
        return new Bool(result);
    }

    public function not(): Bool {
        return new Bool(!this.value);
    }

    // Comparison
    public function equals(Bool other): bool {
        return this.value == other.value;
    }

    // Utility methods (Object interface)
    public function toString(): string {
        return parsePrimitive(this.value);
    }

    public function hashCode(): int {
        return hashCode(this.value);
    }

    // Static factory methods for common values
    // Static factory methods for common values
    public static function getTRUE(): Bool {
        return new Bool(true);
    }

    public static function getFALSE(): Bool {
        return new Bool(false);
    }
}
