// Deque<T> - Double-ended queue supporting element insertion/removal at both ends
// Can be used as both Stack (LIFO) and Queue (FIFO)
// Provides operations for both ends of the collection

import * from "../Collection.mt";
import * from "../Iterator.mt";

interface Deque<T> extends Collection<T> {
    // ===== Front operations =====

    // Adds an element to the front of the deque
    function addFirst(T item): void;

    // Removes and returns the element at the front
    function removeFirst(): T;

    // Returns the element at the front without removing it
    function peekFirst(): T;

    // ===== Rear operations =====

    // Adds an element to the rear of the deque
    function addLast(T item): void;

    // Removes and returns the element at the rear
    function removeLast(): T;

    // Returns the element at the rear without removing it
    function peekLast(): T;

    // ===== Stack operations (LIFO) =====

    // Pushes an element onto the stack (alias for addFirst)
    function push(T item): void;

    // Pops an element from the stack (alias for removeFirst)
    function pop(): T;

    // ===== Queue operations (FIFO) =====

    // Adds to rear (alias for addLast)
    function enqueue(T item): void;

    // Removes from front (alias for removeFirst)
    function dequeue(): T;

    // ===== Inherited from Collection<T> =====
    // function add(T item): bool;  // Alias for addLast
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
