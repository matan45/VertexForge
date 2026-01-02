// ArrayList<T> - Dynamic resizable list implementation
import * from "../../lib/interfaces/List.mt";
import * from "../../lib/Iterator.mt";
import * from "../../lib/iterators/ListIterator.mt";
import * from "../../lib/stream/Stream.mt";
import * from "../../lib/stream/StreamImpl.mt";
import * from "../../lib/exceptions/IndexOutOfBoundsException.mt";
import * from "../../lib/functional/Comparator.mt";
import * from "../../lib/utils/SortUtils.mt";

class ArrayList<T> implements List<T> {
    T[] data;
    int capacity;
    int count;

    public constructor() {
        this.capacity = 10;
        this.data = new T[this.capacity];
        this.count = 0;
    }

    public function add(T item): bool {
        if (item == null) {
            print("Error: List.add() - item cannot be null");
            return false;
        }

        if (this.count >= this.capacity) {
            this.resize();
        }
        this.data[this.count] = item;
        this.count++;
        return true;
    }

    public function get(int index): T {
        if (index < 0 || index >= this.count) {
            throw new IndexOutOfBoundsException("Index " + index + " out of bounds for size " + this.count);
        }
        return this.data[index];
    }

    public function set(int index, T item): void {
        if (index < 0 || index >= this.count) {
            throw new IndexOutOfBoundsException("Index " + index + " out of bounds for size " + this.count);
        }
        if (item == null) {
            print("Error: List.set() - item cannot be null");
            return;
        }
        this.data[index] = item;
    }

    public function contains(T item): bool {
        if (item == null) {
            print("Error: List.contains() - item cannot be null");
            return false;
        }

        for (int i = 0; i < this.count; i++) {
            if (this.data[i] != null && this.data[i].equals(item)) {
                return true;
            }
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
        this.count = 0;
    }

    public function removeAt(int index): bool {
        if (index < 0 || index >= this.count) {
            return false;
        }

        for (int i = index; i < this.count - 1; i++) {
            this.data[i] = this.data[i + 1];
        }
        this.count--;
        return true;
    }

    public function remove(T item): bool {
        if (item == null) {
            print("Error: List.remove() - item cannot be null");
            return false;
        }

        for (int i = 0; i < this.count; i++) {
            if (this.data[i] != null && this.data[i].equals(item)) {
                return this.removeAt(i);
            }
        }
        return false;
    }

    public function first(): T {
        if (this.count == 0) {
            return null;
        }
        return this.data[0];
    }

    public function last(): T {
        if (this.count == 0) {
            return null;
        }
        return this.data[this.count - 1];
    }

    public function toArray(): T[] {
        T[] result = new T[this.count];
        for (int i = 0; i < this.count; i++) {
            result[i] = this.data[i];
        }
        return result;
    }

    public function hashCode(): int {
        int hash = 1;
        for (int i = 0; i < this.count; i++) {
            hash = 31 * hash + hashCode(this.data[i]);
        }
        return hash;
    }

    public function iterator(): Iterator<T> {
        return new ListIterator<T>(this.data, this.count);
    }

    public function stream(): Stream<T> {
        return new StreamImpl<T>(this.iterator());
    }

    public function indexOf(T item): int {
        if (item == null) {
            return -1;
        }
        for (int i = 0; i < this.count; i++) {
            if (this.data[i] != null && this.data[i].equals(item)) {
                return i;
            }
        }
        return -1;
    }

    public function lastIndexOf(T item): int {
        if (item == null) {
            return -1;
        }
        for (int i = this.count - 1; i >= 0; i = i - 1) {
            if (this.data[i] != null && this.data[i].equals(item)) {
                return i;
            }
        }
        return -1;
    }

    public function addAll(T[] items): void {
        for (T item : items) {
            this.add(item);
        }
    }

    public function reverse(): void {
        int left = 0;
        int right = this.count - 1;
        while (left < right) {
            T temp = this.data[left];
            this.data[left] = this.data[right];
            this.data[right] = temp;
            left = left + 1;
            right = right - 1;
        }
    }

    public function sortWith(Comparator<T> comparator): void {
        if (this.count == 0) {
            return;
        }

        T[] sortArray = new T[this.count];
        for (int i = 0; i < this.count; i++) {
            sortArray[i] = this.data[i];
        }

        SortUtils::quicksort(sortArray, comparator);

        for (int i = 0; i < this.count; i++) {
            this.data[i] = sortArray[i];
        }
    }

    function resize(): void {
        this.capacity = this.capacity * 2;
        T[] newData = new T[this.capacity];
        for (int i = 0; i < this.count; i++) {
            newData[i] = this.data[i];
        }
        this.data = newData;
    }
}
