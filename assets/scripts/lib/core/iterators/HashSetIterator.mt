// HashSetIterator<T> - Iterator for HashSet<T> collections
// Iterates over unique elements in a hash-based set

import * from "../Iterator.mt";

class HashSetIterator<T> implements Iterator<T> {
    private T[] elements;
    private int currentIndex;
    private int setSize;

    public constructor(T[] setElements) {
        this.elements = setElements;
        this.currentIndex = 0;
        this.setSize = setElements.length;
    }

    public function hasNext(): bool {
        // Skip null entries in the array
        while (this.currentIndex < this.setSize && this.elements[this.currentIndex] == null) {
            this.currentIndex = this.currentIndex + 1;
        }
        return this.currentIndex < this.setSize;
    }

    public function next(): T {
        if (!this.hasNext()) {
            print("Error: No more elements in iterator");
            return null;
        }
        T element = this.elements[this.currentIndex];
        this.currentIndex = this.currentIndex + 1;
        return element;
    }

    public function close(): void {
        // No resources to clean up
    }
}
