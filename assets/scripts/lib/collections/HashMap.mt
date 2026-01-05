// HashMap<K,V> - Hash table implementation for O(1) operations using 2D arrays
import * from "../../lib/interfaces/Map.mt";
import * from "../../lib/interfaces/MapEntry.mt";
import * from "../../lib/Iterator.mt";
import * from "../../lib/iterators/HashMapKeyIterator.mt";
import * from "../../lib/iterators/HashMapEntryIterator.mt";
import * from "../../lib/iterators/HashMapValueIterator.mt";
import * from "../../lib/stream/Stream.mt";
import * from "../../lib/stream/StreamImpl.mt";

class HashMap<K,V> implements Map<K,V> {
    K[][] keyBuckets;
    V[][] valueBuckets;
    int[] bucketSizes;
    int capacity;
    int count;

    public constructor() {
        this.capacity = 16;
        this.keyBuckets = new K[this.capacity][4];
        this.valueBuckets = new V[this.capacity][4];
        this.bucketSizes = new int[this.capacity];
        this.count = 0;

        for (int i = 0; i < this.capacity; i++) {
            this.bucketSizes[i] = 0;
        }
    }

    public function put(K key, V value): V {
        if (key == null) {
            print("Error: HashMap.put() - key cannot be null");
            return null;
        }

        int bucketIndex = this.getBucketIndex(key);
        int keyIndex = this.findKeyInBucket(bucketIndex, key);

        if (keyIndex >= 0) {
            V oldValue = this.valueBuckets[bucketIndex][keyIndex];
            this.valueBuckets[bucketIndex][keyIndex] = value;
            return oldValue;
        } else {
            if (this.bucketSizes[bucketIndex] >= this.keyBuckets[bucketIndex].length) {
                this.resizeBucket(bucketIndex);
            }

            int newIndex = this.bucketSizes[bucketIndex];
            this.keyBuckets[bucketIndex][newIndex] = key;
            this.valueBuckets[bucketIndex][newIndex] = value;
            this.bucketSizes[bucketIndex] = this.bucketSizes[bucketIndex] + 1;
            this.count++;

            if (this.count > this.capacity * 3 / 4) {
                this.resize();
            }
            return null;
        }
    }

    public function get(K key): V {
        if (key == null) {
            print("Error: HashMap.get() - key cannot be null");
            return null;
        }

        int bucketIndex = this.getBucketIndex(key);
        int keyIndex = this.findKeyInBucket(bucketIndex, key);

        if (keyIndex >= 0) {
            return this.valueBuckets[bucketIndex][keyIndex];
        }
        return null;
    }

    public function containsKey(K key): bool {
        if (key == null) {
            print("Error: HashMap.containsKey() - key cannot be null");
            return false;
        }

        int bucketIndex = this.getBucketIndex(key);
        return this.findKeyInBucket(bucketIndex, key) >= 0;
    }

    public function remove(K key): bool {
        if (key == null) {
            print("Error: HashMap.remove() - key cannot be null");
            return false;
        }

        int bucketIndex = this.getBucketIndex(key);
        int keyIndex = this.findKeyInBucket(bucketIndex, key);

        if (keyIndex >= 0) {
            for (int i = keyIndex; i < this.bucketSizes[bucketIndex] - 1; i++) {
                this.keyBuckets[bucketIndex][i] = this.keyBuckets[bucketIndex][i + 1];
                this.valueBuckets[bucketIndex][i] = this.valueBuckets[bucketIndex][i + 1];
            }
            this.bucketSizes[bucketIndex] = this.bucketSizes[bucketIndex] - 1;
            this.count--;
            return true;
        }
        return false;
    }

    public function size(): int {
        return this.count;
    }

    public function empty(): bool {
        return this.count == 0;
    }

    public function clear(): void {
        for (int i = 0; i < this.capacity; i++) {
            this.bucketSizes[i] = 0;
        }
        this.count = 0;
    }

    public function getKeys(): K[] {
        K[] result = new K[this.count];
        int index = 0;

        for (int bucket = 0; bucket < this.capacity; bucket++) {
            for (int i = 0; i < this.bucketSizes[bucket]; i++) {
                result[index] = this.keyBuckets[bucket][i];
                index++;
            }
        }
        return result;
    }

    public function getValues(): V[] {
        V[] result = new V[this.count];
        int index = 0;

        for (int bucket = 0; bucket < this.capacity; bucket++) {
            for (int i = 0; i < this.bucketSizes[bucket]; i++) {
                result[index] = this.valueBuckets[bucket][i];
                index++;
            }
        }
        return result;
    }

    public function toArray(): K[] {
        return this.getKeys();
    }

    public function hashCode(): int {
        int hash = 0;
        for (int bucket = 0; bucket < this.capacity; bucket++) {
            for (int i = 0; i < this.bucketSizes[bucket]; i++) {
                int keyHash = hashCode(this.keyBuckets[bucket][i]);
                int valueHash = hashCode(this.valueBuckets[bucket][i]);
                hash = hash + keyHash + valueHash;
            }
        }
        return hash;
    }

    public function iterator(): Iterator<K> {
        return new HashMapKeyIterator<K>(this.getKeys());
    }

    public function entryIterator(): Iterator<MapEntry<K, V>> {
        return new HashMapEntryIterator<K, V>(this.getKeys(), this.getValues());
    }

    public function valueIterator(): Iterator<V> {
        return new HashMapValueIterator<V>(this.getValues());
    }

    public function stream(): Stream<K> {
        return new StreamImpl<K>(this.iterator());
    }

    public function streamEntries(): Stream<MapEntry<K, V>> {
        return new StreamImpl<MapEntry<K, V>>(this.entryIterator());
    }

    public function streamValues(): Stream<V> {
        return new StreamImpl<V>(this.valueIterator());
    }

    public function containsValue(V value): bool {
        for (int bucket = 0; bucket < this.capacity; bucket++) {
            for (int i = 0; i < this.bucketSizes[bucket]; i++) {
                V v = this.valueBuckets[bucket][i];
                if (v != null && v.equals(value)) {
                    return true;
                }
                if (v == null && value == null) {
                    return true;
                }
            }
        }
        return false;
    }

    public function putAll(Map<K,V> other): void {
        K[] keys = other.getKeys();
        for (K key : keys) {
            this.put(key, other.get(key));
        }
    }

    function getBucketIndex(K key): int {
        int hash = key.hashCode();

        if (hash < 0) {
            hash = -hash;
            if (hash < 0) {
                hash = hash + 1;
            }
        }

        hash = hash * 1610612741;
        hash = hash + (hash / this.capacity);

        if (hash < 0) {
            hash = -hash;
        }

        return hash % this.capacity;
    }

    function findKeyInBucket(int bucketIndex, K key): int {
        for (int i = 0; i < this.bucketSizes[bucketIndex]; i++) {
            if (this.keyBuckets[bucketIndex][i] != null && this.keyBuckets[bucketIndex][i].equals(key)) {
                return i;
            }
        }
        return -1;
    }

    function resizeBucket(int bucketIndex): void {
        int oldSize = this.keyBuckets[bucketIndex].length;
        int newSize = oldSize * 2;

        K[] newKeys = new K[newSize];
        V[] newValues = new V[newSize];

        for (int i = 0; i < oldSize; i++) {
            newKeys[i] = this.keyBuckets[bucketIndex][i];
            newValues[i] = this.valueBuckets[bucketIndex][i];
        }

        for (int i = 0; i < oldSize; i++) {
            this.keyBuckets[bucketIndex][i] = newKeys[i];
            this.valueBuckets[bucketIndex][i] = newValues[i];
        }
    }

    function resize(): void {
        K[][] oldKeyBuckets = this.keyBuckets;
        V[][] oldValueBuckets = this.valueBuckets;
        int[] oldBucketSizes = this.bucketSizes;
        int oldCapacity = this.capacity;

        this.capacity = this.capacity * 2;
        this.keyBuckets = new K[this.capacity][4];
        this.valueBuckets = new V[this.capacity][4];
        this.bucketSizes = new int[this.capacity];
        this.count = 0;

        for (int i = 0; i < this.capacity; i++) {
            this.bucketSizes[i] = 0;
        }

        for (int bucket = 0; bucket < oldCapacity; bucket++) {
            for (int i = 0; i < oldBucketSizes[bucket]; i++) {
                this.put(oldKeyBuckets[bucket][i], oldValueBuckets[bucket][i]);
            }
        }
    }
}
