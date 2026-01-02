// HashSet<T> - Hash table implementation for O(1) operations
import * from "../../lib/interfaces/Set.mt";
import * from "../../lib/Iterator.mt";
import * from "../../lib/iterators/HashSetIterator.mt";
import * from "../../lib/stream/Stream.mt";
import * from "../../lib/stream/StreamImpl.mt";

class HashSet<T> implements Set<T> {
    T[][] buckets;
    int[] bucketSizes;
    int capacity;
    int count;

    public constructor() {
        this.capacity = 16;
        this.buckets = new T[this.capacity][4];
        this.bucketSizes = new int[this.capacity];
        this.count = 0;

        for (int i = 0; i < this.capacity; i++) {
            this.bucketSizes[i] = 0;
        }
    }

    public function add(T item): bool {
        if (item == null) {
            print("Error: HashSet.add() - item cannot be null");
            return false;
        }

        int bucketIndex = this.getBucketIndex(item);

        if (this.findItemInBucket(bucketIndex, item) >= 0) {
            return false;
        }

        if (this.bucketSizes[bucketIndex] >= this.buckets[bucketIndex].length) {
            this.resizeBucket(bucketIndex);
        }

        int newIndex = this.bucketSizes[bucketIndex];
        this.buckets[bucketIndex][newIndex] = item;
        this.bucketSizes[bucketIndex] = this.bucketSizes[bucketIndex] + 1;
        this.count++;

        if (this.count > this.capacity * 3 / 4) {
            this.resize();
        }

        return true;
    }

    public function contains(T item): bool {
        if (item == null) {
            print("Error: HashSet.contains() - item cannot be null");
            return false;
        }

        int bucketIndex = this.getBucketIndex(item);
        return this.findItemInBucket(bucketIndex, item) >= 0;
    }

    public function remove(T item): bool {
        if (item == null) {
            print("Error: HashSet.remove() - item cannot be null");
            return false;
        }

        int bucketIndex = this.getBucketIndex(item);
        int itemIndex = this.findItemInBucket(bucketIndex, item);

        if (itemIndex >= 0) {
            for (int i = itemIndex; i < this.bucketSizes[bucketIndex] - 1; i++) {
                this.buckets[bucketIndex][i] = this.buckets[bucketIndex][i + 1];
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

    public function toArray(): T[] {
        T[] result = new T[this.count];
        int index = 0;

        for (int bucket = 0; bucket < this.capacity; bucket++) {
            for (int i = 0; i < this.bucketSizes[bucket]; i++) {
                result[index] = this.buckets[bucket][i];
                index++;
            }
        }
        return result;
    }

    public function hashCode(): int {
        int hash = 0;
        for (int bucket = 0; bucket < this.capacity; bucket++) {
            for (int i = 0; i < this.bucketSizes[bucket]; i++) {
                hash = hash + hashCode(this.buckets[bucket][i]);
            }
        }
        return hash;
    }

    public function union(Set<T> other): Set<T> {
        HashSet<T> result = new HashSet<T>();

        T[] thisArray = this.toArray();
        for (T element : thisArray) {
            result.add(element);
        }

        T[] otherArray = other.toArray();
        for (T element : otherArray) {
            result.add(element);
        }

        return result;
    }

    public function intersection(Set<T> other): Set<T> {
        HashSet<T> result = new HashSet<T>();

        T[] thisArray = this.toArray();
        for (T element : thisArray) {
            if (other.contains(element)) {
                result.add(element);
            }
        }

        return result;
    }

    public function difference(Set<T> other): Set<T> {
        HashSet<T> result = new HashSet<T>();

        T[] thisArray = this.toArray();
        for (T element : thisArray) {
            if (!other.contains(element)) {
                result.add(element);
            }
        }

        return result;
    }

    public function isSubsetOf(Set<T> other): bool {
        T[] thisArray = this.toArray();
        for (T element : thisArray) {
            if (!other.contains(element)) {
                return false;
            }
        }
        return true;
    }

    public function isSupersetOf(Set<T> other): bool {
        return other.isSubsetOf(this);
    }

    public function iterator(): Iterator<T> {
        return new HashSetIterator<T>(this.toArray());
    }

    public function stream(): Stream<T> {
        return new StreamImpl<T>(this.iterator());
    }

    public function addAll(T[] items): void {
        for (T item : items) {
            this.add(item);
        }
    }

    function getBucketIndex(T item): int {
        int hash = item.hashCode();

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

    function findItemInBucket(int bucketIndex, T item): int {
        for (int i = 0; i < this.bucketSizes[bucketIndex]; i++) {
            T storedItem = this.buckets[bucketIndex][i];
            if (storedItem != null && storedItem.equals(item)) {
                return i;
            }
        }
        return -1;
    }

    function resizeBucket(int bucketIndex): void {
        int oldSize = this.buckets[bucketIndex].length;
        int newSize = oldSize * 2;

        T[] newBucket = new T[newSize];
        for (int i = 0; i < oldSize; i++) {
            newBucket[i] = this.buckets[bucketIndex][i];
        }

        for (int i = 0; i < oldSize; i++) {
            this.buckets[bucketIndex][i] = newBucket[i];
        }
    }

    function resize(): void {
        T[][] oldBuckets = this.buckets;
        int[] oldBucketSizes = this.bucketSizes;
        int oldCapacity = this.capacity;

        this.capacity = this.capacity * 2;
        this.buckets = new T[this.capacity][4];
        this.bucketSizes = new int[this.capacity];
        this.count = 0;

        for (int i = 0; i < this.capacity; i++) {
            this.bucketSizes[i] = 0;
        }

        for (int bucket = 0; bucket < oldCapacity; bucket++) {
            for (int i = 0; i < oldBucketSizes[bucket]; i++) {
                T item = oldBuckets[bucket][i];
                int newBucketIndex = this.getBucketIndex(item);

                if (this.bucketSizes[newBucketIndex] >= this.buckets[newBucketIndex].length) {
                    this.resizeBucket(newBucketIndex);
                }

                int newIndex = this.bucketSizes[newBucketIndex];
                this.buckets[newBucketIndex][newIndex] = item;
                this.bucketSizes[newBucketIndex] = this.bucketSizes[newBucketIndex] + 1;
                this.count++;
            }
        }
    }
}
