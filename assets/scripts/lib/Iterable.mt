// Iterable<T> - Base interface for all iterable collections
// Provides support for enhanced for-loop syntax: for (T item : collection)
// All collections that can be iterated must implement this interface

interface Iterable<T> {
    // Returns an iterator over elements of type T
    // This enables enhanced for-loop syntax: for (T item : collection)
    function iterator(): Iterator<T>;

    // Convert to array for backward compatibility
    // Legacy iteration: for (T item : collection.toArray())
    function toArray(): T[];
}
