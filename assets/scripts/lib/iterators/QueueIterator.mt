// QueueIterator<T> - Iterator for Queue<T> collections
// Iterates over queue elements from front to rear

import * from "../Iterator.mt";

class QueueIterator<T> implements Iterator<T> {
    private T[] data;
    private int front;
    private int rear;
    private int capacity;
    private int currentIndex;
    private int count;

    public constructor(T[] queueData, int queueFront, int queueRear, int queueCapacity, int queueCount) {
        this.data = queueData;
        this.front = queueFront;
        this.rear = queueRear;
        this.capacity = queueCapacity;
        this.currentIndex = 0;
        this.count = queueCount;
    }

    public function hasNext(): bool {
        return this.currentIndex < this.count;
    }

    public function next(): T {
        if (!this.hasNext()) {
            print("Error: No more elements in iterator");
            return null;
        }
        // Circular array indexing
        int actualIndex = (this.front + this.currentIndex) % this.capacity;
        T element = this.data[actualIndex];
        this.currentIndex = this.currentIndex + 1;
        return element;
    }

    public function close(): void {
        // No resources to clean up
    }
}
