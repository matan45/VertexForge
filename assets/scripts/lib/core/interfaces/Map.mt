// Map<K,V> - Collection of key-value pairs
// Each key maps to at most one value (no duplicate keys)

import * from "../Iterator.mt";
import * from "../Iterable.mt";

interface Map<K,V> extends Iterable<K> {
    // ===== Core operations =====

    // Associates the specified value with the specified key
    // Returns the previous value associated with the key, or null
    function put(K key, V value): V;

    // Returns the value to which the specified key is mapped
    // Returns null if the key is not present
    function get(K key): V;

    // Removes the mapping for a key
    // Returns true if the key was present
    function remove(K key): bool;

    // Returns true if this map contains a mapping for the specified key
    function containsKey(K key): bool;

    // Returns true if this map maps one or more keys to the specified value
    function containsValue(V value): bool;

    // ===== Bulk operations =====

    // Removes all mappings from this map
    function clear(): void;

    // Copies all mappings from the specified map to this map
    function putAll(Map<K,V> other): void;

    // ===== View operations =====

    // Returns an array containing all keys in this map
    function getKeys(): K[];

    // Returns an array containing all values in this map
    function getValues(): V[];

    // ===== Query operations =====

    // Returns the number of key-value mappings in this map
    function size(): int;

    // Returns true if this map contains no key-value mappings
    function empty(): bool;

    // ===== Object methods =====

    // Returns a hash code value for this map
    function hashCode(): int;
}
