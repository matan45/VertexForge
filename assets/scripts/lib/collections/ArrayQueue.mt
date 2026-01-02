// ArrayQueue<T> - FIFO operations
import * from "../../lib/interfaces/Queue.mt";
import * from "../../lib/Iterator.mt";
import * from "../../lib/iterators/QueueIterator.mt";
import * from "../../lib/stream/Stream.mt";
import * from "../../lib/stream/StreamImpl.mt";

class ArrayQueue<T> implements Queue<T> {
    T[] data;
    int capacity;
    int front;
    int rear;
    int count;

    constructor() {
        this.capacity = 4;
        this.data = new T[this.capacity];
        this.front = 0;
        this.rear = 0;
        this.count = 0;
    }

    public function enqueue(T item): void {
        if (this.count >= this.capacity) {
            this.resize();
        }
        this.data[this.rear] = item;
        this.rear = (this.rear + 1) % this.capacity;
        this.count++;
    }

    public function dequeue(): T {
        if (this.empty()) {
            return null;
        }
        T item = this.data[this.front];
        this.front = (this.front + 1) % this.capacity;
        this.count--;
        return item;
    }

    public function peek(): T {
        if (this.empty()) {
            return null;
        }
        return this.data[this.front];
    }

    public function empty(): bool {
        return this.count == 0;
    }

    public function size(): int {
        return this.count;
    }

    public function clear(): void {
        this.front = 0;
        this.rear = 0;
        this.count = 0;
    }

    public function contains(T item): bool {
        for (int i = 0; i < this.count; i++) {
            int index = (this.front + i) % this.capacity;
            if (this.data[index].equals(item)) {
                return true;
            }
        }
        return false;
    }

    public function toArray(): T[] {
        T[] result = new T[this.count];
        for (int i = 0; i < this.count; i++) {
            int index = (this.front + i) % this.capacity;
            result[i] = this.data[index];
        }
        return result;
    }

    public function hashCode(): int {
        int hash = 1;
        for (int i = 0; i < this.count; i++) {
            int index = (this.front + i) % this.capacity;
            hash = 31 * hash + hashCode(this.data[index]);
        }
        return hash;
    }

    public function iterator(): Iterator<T> {
        return new QueueIterator<T>(this.data, this.front, this.rear, this.capacity, this.count);
    }

    public function stream(): Stream<T> {
        return new StreamImpl<T>(this.iterator());
    }

    public function add(T item): bool {
        this.enqueue(item);
        return true;
    }

    public function remove(T item): bool {
        return false;
    }

    public function addAll(T[] items): void {
        for (T item : items) {
            this.enqueue(item);
        }
    }

    function resize(): void {
        int oldCapacity = this.capacity;
        this.capacity = this.capacity * 2;
        T[] newData = new T[this.capacity];

        for (int i = 0; i < this.count; i++) {
            newData[i] = this.data[(this.front + i) % oldCapacity];
        }

        this.data = newData;
        this.front = 0;
        this.rear = this.count;
    }
}
