// LinkedList<T> - Doubly linked list implementation for O(1) insertions and deletions
import * from "../../lib/interfaces/List.mt";
import * from "../../lib/interfaces/Deque.mt";
import * from "../../lib/Iterator.mt";
import * from "../../lib/iterators/LinkedListIterator.mt";
import * from "../../lib/stream/Stream.mt";
import * from "../../lib/stream/StreamImpl.mt";
import * from "../../lib/internal/Node.mt";
import * from "../../lib/functional/Comparator.mt";
import * from "../../lib/utils/SortUtils.mt";

class LinkedList<T> implements List<T>, Deque<T> {

    Node<T> head;
    Node<T> tail;
    int count;

    constructor() {
        this.head = null;
        this.tail = null;
        this.count = 0;
    }

    public function add(T item): bool {
        if (item == null) {
            print("Error: LinkedList.add() - item cannot be null");
            return false;
        }

        Node<T> newNode = new Node<T>(item);

        if (this.head == null) {
            this.head = newNode;
            this.tail = newNode;
        } else {
            newNode.prev = this.tail;
            this.tail.next = newNode;
            this.tail = newNode;
        }
        this.count++;
        return true;
    }

    public function addFirst(T item): void {
        if (item == null) {
            print("Error: LinkedList.addFirst() - item cannot be null");
            return;
        }

        Node<T> newNode = new Node<T>(item);

        if (this.head == null) {
            this.head = newNode;
            this.tail = newNode;
        } else {
            newNode.next = this.head;
            this.head.prev = newNode;
            this.head = newNode;
        }
        this.count++;
    }

    public function addLast(T item): void {
        this.add(item);
    }

    public function addAt(int index, T item): void {
        if (item == null) {
            print("Error: LinkedList.addAt() - item cannot be null");
            return;
        }

        if (index < 0 || index > this.count) {
            print("Error: LinkedList.addAt() - index out of bounds: " + index);
            return;
        }

        if (index == 0) {
            this.addFirst(item);
            return;
        }

        if (index == this.count) {
            this.addLast(item);
            return;
        }

        Node<T> current = this.getNodeAt(index);
        Node<T> newNode = new Node<T>(item, current.prev, current);

        current.prev.next = newNode;
        current.prev = newNode;
        this.count++;
    }

    public function get(int index): T {
        if (index < 0 || index >= this.count) {
            print("Error: LinkedList.get() - index out of bounds: " + index);
            return null;
        }

        Node<T> node = this.getNodeAt(index);
        return node.data;
    }

    public function set(int index, T item): void {
        if (index < 0 || index >= this.count) {
            print("Error: LinkedList.set() - index out of bounds: " + index);
            return;
        }

        if (item == null) {
            print("Error: LinkedList.set() - item cannot be null");
            return;
        }

        Node<T> node = this.getNodeAt(index);
        node.data = item;
    }

    public function removeAt(int index): bool {
        if (index < 0 || index >= this.count) {
            print("Error: LinkedList.removeAt() - index out of bounds: " + index);
            return false;
        }

        Node<T> nodeToRemove = this.getNodeAt(index);
        this.removeNode(nodeToRemove);
        return true;
    }

    public function remove(T item): bool {
        if (item == null) {
            print("Error: LinkedList.remove() - item cannot be null");
            return false;
        }

        Node<T> current = this.head;
        while (current != null) {
            if (current.data != null && current.data.equals(item)) {
                this.removeNode(current);
                return true;
            }
            current = current.next;
        }
        return false;
    }

    public function removeFirst(): T {
        if (this.head == null) {
            print("Error: LinkedList.removeFirst() - list is empty");
            return null;
        }

        T data = this.head.data;
        this.removeNode(this.head);
        return data;
    }

    public function removeLast(): T {
        if (this.tail == null) {
            print("Error: LinkedList.removeLast() - list is empty");
            return null;
        }

        T data = this.tail.data;
        this.removeNode(this.tail);
        return data;
    }

    public function contains(T item): bool {
        if (item == null) {
            print("Error: LinkedList.contains() - item cannot be null");
            return false;
        }

        Node<T> current = this.head;
        while (current != null) {
            if (current.data != null && current.data.equals(item)) {
                return true;
            }
            current = current.next;
        }
        return false;
    }

    public function indexOf(T item): int {
        if (item == null) {
            print("Error: LinkedList.indexOf() - item cannot be null");
            return -1;
        }

        Node<T> current = this.head;
        int index = 0;
        while (current != null) {
            if (current.data != null && current.data.equals(item)) {
                return index;
            }
            current = current.next;
            index++;
        }
        return -1;
    }

    public function lastIndexOf(T item): int {
        if (item == null) {
            return -1;
        }

        Node<T> current = this.tail;
        int index = this.count - 1;
        while (current != null) {
            if (current.data != null && current.data.equals(item)) {
                return index;
            }
            current = current.prev;
            index = index - 1;
        }
        return -1;
    }

    public function size(): int {
        return this.count;
    }

    public function empty(): bool {
        return this.count == 0;
    }

    public function clear(): void {
        this.head = null;
        this.tail = null;
        this.count = 0;
    }

    public function first(): T {
        if (this.head == null) {
            return null;
        }
        return this.head.data;
    }

    public function last(): T {
        if (this.tail == null) {
            return null;
        }
        return this.tail.data;
    }

    public function toArray(): T[] {
        T[] result = new T[this.count];
        Node<T> current = this.head;
        int index = 0;

        while (current != null && index < this.count) {
            result[index] = current.data;
            current = current.next;
            index++;
        }
        return result;
    }

    public function hashCode(): int {
        int hash = 1;
        Node<T> current = this.head;

        while (current != null) {
            if (current.data != null) {
                hash = 31 * hash + hashCode(current.data);
            } else {
                hash = 31 * hash;
            }
            current = current.next;
        }
        return hash;
    }

    public function reverse(): void {
        if (this.count <= 1) {
            return;
        }

        Node<T> current = this.head;
        Node<T> temp = null;

        while (current != null) {
            temp = current.prev;
            current.prev = current.next;
            current.next = temp;
            current = current.prev;
        }

        temp = this.head;
        this.head = this.tail;
        this.tail = temp;
    }

    public function addAll(T[] items): void {
        for (T item : items) {
            this.add(item);
        }
    }

    public function removeAll(T item): int {
        if (item == null) {
            print("Error: LinkedList.removeAll() - item cannot be null");
            return 0;
        }

        int removedCount = 0;
        Node<T> current = this.head;

        while (current != null) {
            Node<T> next = current.next;
            if (current.data != null && current.data.equals(item)) {
                this.removeNode(current);
                removedCount++;
            }
            current = next;
        }

        return removedCount;
    }

    public function subList(int fromIndex, int toIndex): LinkedList<T> {
        if (fromIndex < 0 || toIndex > this.count || fromIndex > toIndex) {
            print("Error: LinkedList.subList() - invalid range: " + fromIndex + " to " + toIndex);
            return new LinkedList<T>();
        }

        LinkedList<T> result = new LinkedList<T>();
        Node<T> current = this.getNodeAt(fromIndex);

        for (int i = fromIndex; i < toIndex && current != null; i++) {
            result.add(current.data);
            current = current.next;
        }

        return result;
    }

    public function iterator(): Iterator<T> {
        return new LinkedListIterator<T>(this.head);
    }

    public function stream(): Stream<T> {
        return new StreamImpl<T>(this.iterator());
    }

    public function sortWith(Comparator<T> comparator): void {
        if (this.count == 0) {
            return;
        }

        T[] sortArray = this.toArray();
        SortUtils::quicksort(sortArray, comparator);

        this.clear();
        for (int i = 0; i < sortArray.length; i++) {
            this.add(sortArray[i]);
        }
    }

    public function peekFirst(): T {
        return this.first();
    }

    public function peekLast(): T {
        return this.last();
    }

    public function push(T item): void {
        this.addFirst(item);
    }

    public function pop(): T {
        return this.removeFirst();
    }

    public function enqueue(T item): void {
        this.addLast(item);
    }

    public function dequeue(): T {
        return this.removeFirst();
    }

    private function getNodeAt(int index): Node<T> {
        if (index < 0 || index >= this.count) {
            return null;
        }

        Node<T> current;

        if (index < this.count / 2) {
            current = this.head;
            for (int i = 0; i < index; i++) {
                current = current.next;
            }
        } else {
            current = this.tail;
            for (int i = this.count - 1; i > index; i--) {
                current = current.prev;
            }
        }

        return current;
    }

    private function removeNode(Node<T> node): void {
        if (node == null) {
            return;
        }

        if (node.prev != null) {
            node.prev.next = node.next;
        } else {
            this.head = node.next;
        }

        if (node.next != null) {
            node.next.prev = node.prev;
        } else {
            this.tail = node.prev;
        }

        this.count--;
    }
}
