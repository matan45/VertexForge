// Queue<T> - Collection for FIFO (First-In-First-Out) operations
// Typically orders elements in the order they were inserted
// Provides queue-specific operations

import * from "../Collection.mt";
import * from "../Iterator.mt";

interface Queue<T> extends Collection<T> {
    // ===== Queue-specific operations =====

    // Adds an element to the rear of the queue
    function enqueue(T item): void;

    // Removes and returns the element at the front of the queue
    function dequeue(): T;

    // Returns the element at the front without removing it
    function peek(): T;

    // ===== Inherited from Collection<T> =====
    // function add(T item): bool;  // Alias for enqueue
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
