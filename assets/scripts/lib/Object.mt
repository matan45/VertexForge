// Object - Base interface for all objects in mType
// Provides common methods that all objects should implement
interface Object<T> {
    // String representation of the object
    function toString(): string;

    // Equality comparison
    function equals(T other): bool;

    // Hash code for hash-based collections
    function hashCode(): int;
}
