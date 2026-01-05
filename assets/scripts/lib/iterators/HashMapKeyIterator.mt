// HashMapKeyIterator<K> - Iterator for HashMap<K,V> keys
// Iterates over keys in a hash-based map

import * from "../Iterator.mt";

class HashMapKeyIterator<K> implements Iterator<K> {
    private K[] keys;
    private int currentIndex;
    private int keysCount;

    public constructor(K[] mapKeys) {
        this.keys = mapKeys;
        this.currentIndex = 0;
        this.keysCount = mapKeys.length;
    }

    public function hasNext(): bool {
        // Skip null entries in the keys array
        while (this.currentIndex < this.keysCount && this.keys[this.currentIndex] == null) {
            this.currentIndex = this.currentIndex + 1;
        }
        return this.currentIndex < this.keysCount;
    }

    public function next(): K {
        if (!this.hasNext()) {
            print("Error: No more elements in iterator");
            return null;
        }
        K key = this.keys[this.currentIndex];
        this.currentIndex = this.currentIndex + 1;
        return key;
    }

    public function close(): void {
        // No resources to clean up
    }
}
