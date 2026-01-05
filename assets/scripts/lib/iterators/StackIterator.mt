// StackIterator<T> - Iterator for Stack<T> collections
// Iterates from top to bottom of the stack

import * from "../Iterator.mt";

class StackIterator<T> implements Iterator<T> {
    private T[] data;
    private int topIndex;
    private int currentIndex;

    public constructor(T[] stackData, int stackTop) {
        this.data = stackData;
        this.topIndex = stackTop;
        this.currentIndex = stackTop;  // Start from top
    }

    public function hasNext(): bool {
        return this.currentIndex >= 0;
    }

    public function next(): T {
        if (!this.hasNext()) {
            print("Error: No more elements in iterator");
            return null;
        }
        T element = this.data[this.currentIndex];
        this.currentIndex = this.currentIndex - 1;
        return element;
    }

    public function close(): void {
        // No resources to clean up
    }
}
