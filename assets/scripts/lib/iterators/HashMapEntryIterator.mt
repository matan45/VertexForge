/**
 * Iterator for HashMap entries (key-value pairs).
 * Iterates over MapEntry objects representing each entry in the map.
 *
 * @param K the type of keys
 * @param V the type of values
 */
import * from "../Iterator.mt";
import * from "../interfaces/MapEntry.mt";

class HashMapEntryIterator<K, V> implements Iterator<MapEntry<K, V>> {
    private K[] keys;
    private V[] values;
    private int currentIndex;
    private int entriesCount;

    public constructor(K[] mapKeys, V[] mapValues) {
        this.keys = mapKeys;
        this.values = mapValues;
        this.currentIndex = 0;
        this.entriesCount = mapKeys.length;
    }

    public function hasNext(): bool {
        // Skip null entries in the keys array
        while (this.currentIndex < this.entriesCount && this.keys[this.currentIndex] == null) {
            this.currentIndex = this.currentIndex + 1;
        }
        return this.currentIndex < this.entriesCount;
    }

    public function next(): MapEntry<K, V> {
        if (!this.hasNext()) {
            throw "No more elements in HashMapEntryIterator";
        }
        K key = this.keys[this.currentIndex];
        V value = this.values[this.currentIndex];
        this.currentIndex = this.currentIndex + 1;
        return new MapEntry<K, V>(key, value);
    }

    public function close(): void {
        // No resources to clean up
    }
}
