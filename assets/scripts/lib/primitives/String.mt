// String - Object wrapper for string values
// Provides a pure OOP interface for string operations
// Internally uses string pooling for memory efficiency
import * from "../Object.mt";
public value class String implements Object<String> {
    private string value;

    // Constructors
    public constructor(string val) {
        this.value = val;
    }

    public constructor() {
        this.value = "";
    }

    // Accessors
    public function getValue(): string {
        return this.value;
    }

    public function setValue(string val): void {
        this.value = val;
    }

    // String operations
    public function length(): int {
        return strLength(this.value);
    }

    public function isEmpty(): bool {
        return this.value == "";
    }

    public function concat(String other): String {
        return new String(this.getValue() + other.getValue());
    }

    // Comparison operations
    public function equals(String other): bool {
        return this.value == other.value;
    }

    public function compareTo(String other): int {
        // Lexicographic comparison
        if (this.value < other.value) return -1;
        if (this.value > other.value) return 1;
        return 0;
    }

    // Utility methods (Object interface)
    public function toString(): string {
        return this.value;
    }

    public function hashCode(): int {
        return hashCode(this.value);
    }

    // Character operations
    public function startsWith(String prefix): bool {
        int prefixLen = prefix.length();
        if (prefixLen > this.length()) {
            return false;
        }
        if (prefixLen == 0) {
            return true;
        }
        int idx = indexOf(this.value, prefix.value);
        return idx == 0;
    }

    public function endsWith(String suffix): bool {
        int suffixLen = suffix.length();
        int thisLen = this.length();
        if (suffixLen > thisLen) {
            return false;
        }
        if (suffixLen == 0) {
            return true;
        }
        string sub = substring(this.value, thisLen - suffixLen, suffixLen);
        return sub == suffix.value;
    }

    public function contains(String substring): bool {
        int idx = indexOf(this.value, substring.value);
        return idx != -1;
    }

    // Substring operations
    public function substring(int startIndex, int length): String {
        string sub = substring(this.value, startIndex, length);
        return new String(sub);
    }

    // Case conversion operations
    public function toUpperCase(): String {
        string upper = toUpperCase(this.value);
        return new String(upper);
    }

    public function toLowerCase(): String {
        string lower = toLowerCase(this.value);
        return new String(lower);
    }

    // Index operations
    public function indexOf(String needle): int {
        return indexOf(this.value, needle.value);
    }
}
