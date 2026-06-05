/**
 * Iterator for HashMap values.
 * Iterates over the values in a hash-based map.
 *
 * @param V the type of values
 */
import * from "../Iterator.mt";

class HashMapValueIterator<V> implements Iterator<V> {
    private V[] values;
    private int currentIndex;
    private int valuesCount;

    public constructor(V[] mapValues) {
        this.values = mapValues;
        this.currentIndex = 0;
        this.valuesCount = mapValues.length;
    }

    public function hasNext(): bool {
        return this.currentIndex < this.valuesCount;
    }

    public function next(): V {
        if (!this.hasNext()) {
            throw "No more elements in HashMapValueIterator";
        }
        V value = this.values[this.currentIndex];
        this.currentIndex = this.currentIndex + 1;
        return value;
    }

    public function close(): void {
        // No resources to clean up
    }
}
