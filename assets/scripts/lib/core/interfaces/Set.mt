// Set<T> - Collection that contains no duplicate elements
// Order is not guaranteed
// Uses hash-based implementation for O(1) operations

import * from "../Collection.mt";
import * from "../Iterator.mt";

interface Set<T> extends Collection<T> {
    // ===== Set-specific operations =====

    // Returns a new set containing all elements from this set and the other set
    function union(Set<T> other): Set<T>;

    // Returns a new set containing elements present in both sets
    function intersection(Set<T> other): Set<T>;

    // Returns a new set containing elements in this set but not in the other
    function difference(Set<T> other): Set<T>;

    // Returns true if this set is a subset of the other set
    function isSubsetOf(Set<T> other): bool;

    // Returns true if this set is a superset of the other set
    function isSupersetOf(Set<T> other): bool;

    // ===== Inherited from Collection<T> =====
    // function add(T item): bool;  // Returns false if already exists
    // function remove(T item): bool;
    // function contains(T item): bool;
    // function size(): int;
    // function empty(): bool;
    // function clear(): void;
    // function addAll(T[] items): void;
    // function hashCode(): int;
    // function iterator(): Iterator<T>;
    // function toArray(): T[];
}
