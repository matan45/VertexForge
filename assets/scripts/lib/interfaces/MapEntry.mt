/**
 * A map entry (key-value pair).
 * Provides methods to get the key and value of an entry in a Map.
 *
 * @param K the type of keys
 * @param V the type of values
 */
class MapEntry<K, V> {
    private K key;
    private V value;

    public constructor(K key, V value) {
        this.key = key;
        this.value = value;
    }

    /**
     * Returns the key corresponding to this entry.
     */
    public function getKey(): K {
        return this.key;
    }

    /**
     * Returns the value corresponding to this entry.
     */
    public function getValue(): V {
        return this.value;
    }

    /**
     * Replaces the value corresponding to this entry with the specified value.
     */
    public function setValue(V newValue): V {
        V oldValue = this.value;
        this.value = newValue;
        return oldValue;
    }

    /**
     * Compares the specified entry with this entry for equality.
     */
    public function equals(MapEntry<K, V> other): bool {
        if (other == null) return false;

        bool keysEqual = (this.key == null && other.getKey() == null) ||
                        (this.key != null && this.key.equals(other.getKey()));
        bool valuesEqual = (this.value == null && other.getValue() == null) ||
                          (this.value != null && this.value.equals(other.getValue()));

        return keysEqual && valuesEqual;
    }

    /**
     * Returns the hash code value for this map entry.
     */
    public function hashCode(): int {
        int keyHash = (this.key == null) ? 0 : this.key.hashCode();
        int valueHash = (this.value == null) ? 0 : this.value.hashCode();
        return keyHash + valueHash;
    }

    /**
     * Returns a string representation of this map entry.
     */
    public function toString(): String {
        return this.key.toString() + "=" + this.value.toString();
    }
}
