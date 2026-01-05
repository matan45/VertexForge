// Node<T> - Internal node class for LinkedList implementation
// This is a private implementation detail shared between LinkedList and LinkedListIterator

class Node<T> {
    public T data;
    public Node<T> next;
    public Node<T> prev;

    constructor(T data) {
        this.data = data;
        this.next = null;
        this.prev = null;
    }

    constructor(T data, Node<T> prev, Node<T> next) {
        this.data = data;
        this.prev = prev;
        this.next = next;
    }
}
