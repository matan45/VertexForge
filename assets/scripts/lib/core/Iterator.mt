// Iterator<T> - Interface for iterating over a collection
// Provides standard iteration protocol used by enhanced for-loops

interface Iterator<T> {
    // Returns true if the iteration has more elements
    function hasNext(): bool;

    // Returns the next element in the iteration
    // Should only be called when hasNext() returns true
    function next(): T;

    // Optional: Close/cleanup the iterator
    // Used for resource cleanup (files, streams, etc.)
    // Default implementation does nothing
    function close(): void;
}
