// Collection<T> - Common interface for all collections
// Provides core operations shared by List, Set, Queue, etc.
// Extends Iterable<T> to support enhanced for-loop syntax

import * from "Iterator.mt";
import * from "Iterable.mt";

interface Collection<T> extends Iterable<T> {
    // ===== Core modification operations =====

    // Adds an element to this collection
    // Returns true if the collection changed (false for duplicates in Set)
    function add(T item): bool;

    // Removes the first occurrence of the specified element
    // Returns true if an element was removed
    function remove(T item): bool;

    // Removes all elements from this collection
    function clear(): void;

    // ===== Query operations =====

    // Returns true if this collection contains the specified element
    function contains(T item): bool;

    // Returns the number of elements in this collection
    function size(): int;

    // Returns true if this collection contains no elements
    function empty(): bool;

    // ===== Bulk operations =====

    // Adds all elements from the array to this collection
    function addAll(T[] items): void;

    // ===== Object methods =====

    // Returns a hash code value for this collection
    function hashCode(): int;

    // ===== Stream operations =====

    // Returns a sequential Stream with this collection as its source
    // function stream(): Stream<T>;  // Requires Stream<T> import

    // ===== Inherited from Iterable<T> =====
    // function iterator(): Iterator<T>;
    // function toArray(): T[];
}
