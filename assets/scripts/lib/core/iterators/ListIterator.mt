// ListIterator<T> - Iterator for List<T> collections
// Provides efficient iteration over array-based lists

import * from "../Iterator.mt";

class ListIterator<T> implements Iterator<T> {
    private T[] data;
    private int currentIndex;
    private int listSize;

    public constructor(T[] listData, int size) {
        this.data = listData;
        this.currentIndex = 0;
        this.listSize = size;
    }

    public function hasNext(): bool {
        return this.currentIndex < this.listSize;
    }

    public function next(): T {
        if (!this.hasNext()) {
            print("Error: No more elements in iterator");
            return null;
        }
        T element = this.data[this.currentIndex];
        this.currentIndex = this.currentIndex + 1;
        return element;
    }

    public function close(): void {
        // No resources to clean up
    }
}
