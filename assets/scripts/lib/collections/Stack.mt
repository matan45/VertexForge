// Stack<T> - LIFO operations
import * from "../../lib/interfaces/Deque.mt";
import * from "../../lib/Iterator.mt";
import * from "../../lib/iterators/StackIterator.mt";
import * from "../../lib/stream/Stream.mt";
import * from "../../lib/stream/StreamImpl.mt";

class Stack<T> implements Deque<T> {
    T[] data;
    int capacity;
    int top;

    constructor() {
        this.capacity = 4;
        this.data = new T[this.capacity];
        this.top = -1;
    }

    public function push(T item): void {
        if (this.top + 1 >= this.capacity) {
            this.resize();
        }
        this.top++;
        this.data[this.top] = item;
    }

    public function pop(): T {
        if (this.empty()) {
            return null;
        }
        T item = this.data[this.top];
        this.top--;
        return item;
    }

    public function peek(): T {
        if (this.empty()) {
            return null;
        }
        return this.data[this.top];
    }

    public function empty(): bool {
        return this.top < 0;
    }

    public function size(): int {
        return this.top + 1;
    }

    public function clear(): void {
        this.top = -1;
    }

    public function contains(T item): bool {
        for (int i = 0; i <= this.top; i++) {
            if (this.data[i].equals(item)) {
                return true;
            }
        }
        return false;
    }

    public function toArray(): T[] {
        T[] result = new T[this.top + 1];
        for (int i = 0; i <= this.top; i++) {
            result[i] = this.data[i];
        }
        return result;
    }

    public function hashCode(): int {
        int hash = 1;
        for (int i = 0; i <= this.top; i++) {
            hash = 31 * hash + hashCode(this.data[i]);
        }
        return hash;
    }

    public function iterator(): Iterator<T> {
        return new StackIterator<T>(this.data, this.top);
    }

    public function stream(): Stream<T> {
        return new StreamImpl<T>(this.iterator());
    }

    public function add(T item): bool {
        this.push(item);
        return true;
    }

    public function addAll(T[] items): void {
        for (T item : items) {
            this.push(item);
        }
    }

    public function remove(T item): bool {
        return false;
    }

    public function addFirst(T item): void {
        this.push(item);
    }

    public function removeFirst(): T {
        return this.pop();
    }

    public function peekFirst(): T {
        return this.peek();
    }

    public function addLast(T item): void {
        // Not efficient for stack
    }

    public function removeLast(): T {
        return null;
    }

    public function peekLast(): T {
        if (this.empty()) {
            return null;
        }
        return this.data[0];
    }

    public function enqueue(T item): void {
        this.push(item);
    }

    public function dequeue(): T {
        return this.pop();
    }

    function resize(): void {
        this.capacity = this.capacity * 2;
        T[] newData = new T[this.capacity];
        for (int i = 0; i <= this.top; i++) {
            newData[i] = this.data[i];
        }
        this.data = newData;
    }
}
