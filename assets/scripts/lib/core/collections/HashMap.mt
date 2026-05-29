// HashMap<K,V> - Open-addressing hash table with linear probing on flat 1D arrays
//
// HASH-TO-SLOT FORMULA (single source of truth):
//     int mixed = rawHash * 1610612741;
//     int idx   = (mixed ^ (mixed >> 16)) & (capacity - 1);
//
// THIS FORMULA IS DUPLICATED IN NATIVE CODE. If you change it here, you MUST
// also update every site below — silent divergence breaks containsKey/get
// for any HashMap instance constructed via reflection or interop:
//   - mType/json/JsonDeserializer.cpp :: deserializeHashMapCollection
//   - mType/json/JsonDeserializer.cpp :: computeBucketIndex (helper)
//   - mType/net/HashMapMarshal.cpp    :: stdMapToHashMap (computeSlotIndex)
// HashSet.mt mirrors the same formula; keep them in sync.

import * from "../interfaces/Map.mt";
import * from "../interfaces/MapEntry.mt";
import * from "../Iterator.mt";
import * from "../iterators/HashMapKeyIterator.mt";
import * from "../iterators/HashMapEntryIterator.mt";
import * from "../iterators/HashMapValueIterator.mt";
import * from "../stream/Stream.mt";
import * from "../stream/StreamImpl.mt";

// Storage layout: 4 parallel flat arrays of length `capacity` (power of 2).
// `keys[i] == null` marks an empty slot — terminates probe sequences.
// Null keys are forbidden by the public API, so this sentinel is unambiguous.
class HashMap<K,V> implements Map<K,V> {
    K[] keys;
    V[] values;
    int[] hashes;
    int capacity;
    int count;

    public constructor() {
        this.capacity = 32;
        this.keys = new K[this.capacity];
        this.values = new V[this.capacity];
        this.hashes = new int[this.capacity];
        this.count = 0;
    }

    // Accepts any int; rounds up to the next power of two >= 4. Required
    // because the slot index uses `& (capacity - 1)` as its modulo, which
    // is only correct when capacity is a power of two.
    public constructor(int initialCapacity) {
        int cap = 4;
        while (cap < initialCapacity) {
            cap = cap * 2;
        }
        this.capacity = cap;
        this.keys = new K[this.capacity];
        this.values = new V[this.capacity];
        this.hashes = new int[this.capacity];
        this.count = 0;
    }

    public function put(K key, V value): V {
        if (key == null) {
            print("Error: HashMap.put() - key cannot be null");
            return null;
        }

        int rawHash = key.hashCode();
        int mixed = rawHash * 1610612741;
        int mask = this.capacity - 1;
        int idx = (mixed ^ (mixed >> 16)) & mask;

        while (this.keys[idx] != null) {
            if (this.hashes[idx] == rawHash && this.keys[idx].equals(key)) {
                V oldValue = this.values[idx];
                this.values[idx] = value;
                return oldValue;
            }
            idx = (idx + 1) & mask;
        }

        this.keys[idx] = key;
        this.values[idx] = value;
        this.hashes[idx] = rawHash;
        this.count++;

        if (this.count > this.capacity * 3 / 4) {
            this.resize();
        }
        return null;
    }

    public function get(K key): V {
        if (key == null) {
            print("Error: HashMap.get() - key cannot be null");
            return null;
        }

        int rawHash = key.hashCode();
        int mixed = rawHash * 1610612741;
        int mask = this.capacity - 1;
        int idx = (mixed ^ (mixed >> 16)) & mask;

        while (this.keys[idx] != null) {
            if (this.hashes[idx] == rawHash && this.keys[idx].equals(key)) {
                return this.values[idx];
            }
            idx = (idx + 1) & mask;
        }
        return null;
    }

    public function containsKey(K key): bool {
        if (key == null) {
            print("Error: HashMap.containsKey() - key cannot be null");
            return false;
        }

        int rawHash = key.hashCode();
        int mixed = rawHash * 1610612741;
        int mask = this.capacity - 1;
        int idx = (mixed ^ (mixed >> 16)) & mask;

        while (this.keys[idx] != null) {
            if (this.hashes[idx] == rawHash && this.keys[idx].equals(key)) {
                return true;
            }
            idx = (idx + 1) & mask;
        }
        return false;
    }

    public function remove(K key): bool {
        if (key == null) {
            print("Error: HashMap.remove() - key cannot be null");
            return false;
        }

        int rawHash = key.hashCode();
        int mixed = rawHash * 1610612741;
        int mask = this.capacity - 1;
        int idx = (mixed ^ (mixed >> 16)) & mask;

        while (this.keys[idx] != null) {
            if (this.hashes[idx] == rawHash && this.keys[idx].equals(key)) {
                // Knuth Algorithm 6.4R back-shift. Two cursors:
                //   hole  — empty slot we are trying to fill (only advances on shift)
                //   probe — scan cursor (always advances)
                // For each entry at `probe` with ideal slot kIdeal:
                //   If kIdeal is in cyclic range (hole, probe] the entry is
                //   "settled" — moving it back would put it before its ideal,
                //   violating the probe-sequence invariant. Skip it.
                //   Otherwise the entry probed past hole on insertion, so we
                //   shift it down to hole and make probe's old slot the new hole.
                int hole = idx;
                int probe = (hole + 1) & mask;
                while (this.keys[probe] != null) {
                    int hj = this.hashes[probe];
                    int mj = hj * 1610612741;
                    int kIdeal = (mj ^ (mj >> 16)) & mask;

                    bool inRange = false;
                    if (hole < probe) {
                        inRange = (kIdeal > hole && kIdeal <= probe);
                    } else {
                        inRange = (kIdeal > hole || kIdeal <= probe);
                    }

                    if (!inRange) {
                        this.keys[hole] = this.keys[probe];
                        this.values[hole] = this.values[probe];
                        this.hashes[hole] = this.hashes[probe];
                        hole = probe;
                    }
                    probe = (probe + 1) & mask;
                }
                this.keys[hole] = null;
                this.count--;
                return true;
            }
            idx = (idx + 1) & mask;
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
            this.keys[i] = null;
        }
        this.count = 0;
    }

    public function getKeys(): K[] {
        K[] result = new K[this.count];
        int index = 0;
        for (int i = 0; i < this.capacity; i++) {
            if (this.keys[i] != null) {
                result[index] = this.keys[i];
                index++;
            }
        }
        return result;
    }

    public function getValues(): V[] {
        V[] result = new V[this.count];
        int index = 0;
        for (int i = 0; i < this.capacity; i++) {
            if (this.keys[i] != null) {
                result[index] = this.values[i];
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
        for (int i = 0; i < this.capacity; i++) {
            if (this.keys[i] != null) {
                int keyHash = hashCode(this.keys[i]);
                int valueHash = hashCode(this.values[i]);
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
        for (int i = 0; i < this.capacity; i++) {
            if (this.keys[i] != null) {
                V v = this.values[i];
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
        K[] otherKeys = other.getKeys();
        for (K key : otherKeys) {
            this.put(key, other.get(key));
        }
    }

    function resize(): void {
        K[] oldKeys = this.keys;
        V[] oldValues = this.values;
        int[] oldHashes = this.hashes;
        int oldCapacity = this.capacity;

        this.capacity = this.capacity * 2;
        this.keys = new K[this.capacity];
        this.values = new V[this.capacity];
        this.hashes = new int[this.capacity];
        this.count = 0;

        int mask = this.capacity - 1;
        for (int slot = 0; slot < oldCapacity; slot++) {
            K key = oldKeys[slot];
            if (key != null) {
                int rawHash = oldHashes[slot];
                int mixed = rawHash * 1610612741;
                int idx = (mixed ^ (mixed >> 16)) & mask;
                while (this.keys[idx] != null) {
                    idx = (idx + 1) & mask;
                }
                this.keys[idx] = key;
                this.values[idx] = oldValues[slot];
                this.hashes[idx] = rawHash;
                this.count++;
            }
        }
    }
}
