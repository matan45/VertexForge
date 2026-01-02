import * from "../Iterator.mt";
import * from "../Iterable.mt";
import * from "Stream.mt";
import * from "StreamImpl.mt";
import * from "../primitives/Int.mt";
import * from "../exceptions/RuntimeException.mt";
/**
 * Utility class for creating Stream instances from various sources.
 * Provides factory methods similar to Java's Stream class.
 *
 * Example:
 *   Stream<int> stream = Streams::of<int>([1, 2, 3, 4, 5]);
 *   Stream<String> empty = Streams.empty<String>();
 *   Stream<int> range = Streams.range(0, 10);
 */
class Streams {
    /**
     * Returns a sequential ordered stream whose elements are the specified values.
     *
     * @param elements the elements of the new stream
     * @return the new stream
     */
    public static function <T> of(T[] elements): Stream<T> {
        Iterator<T> iterator = new ArrayStreamIterator<T>(elements);
        return new StreamImpl<T>(iterator);
    }

    /**
     * Returns an empty sequential stream.
     *
     * @return an empty sequential stream
     */
    public static function <T> empty(): Stream<T> {
        Iterator<T> iterator = new EmptyIterator<T>();
        return new StreamImpl<T>(iterator);
    }

    /**
     * Returns a sequential ordered stream of integers from startInclusive (inclusive)
     * to endExclusive (exclusive) by an incremental step of 1.
     *
     * @param startInclusive the (inclusive) initial value
     * @param endExclusive the exclusive upper bound
     * @return a sequential stream for the range of int elements
     */
    public static function range(int startInclusive, int endExclusive): Stream<Int> {
        Iterator<Int> iterator = new RangeIterator(startInclusive, endExclusive);
        return new StreamImpl<Int>(iterator);
    }

    /**
     * Returns a sequential ordered stream of integers from startInclusive (inclusive)
     * to endInclusive (inclusive) by an incremental step of 1.
     *
     * @param startInclusive the (inclusive) initial value
     * @param endInclusive the inclusive upper bound
     * @return a sequential stream for the range of int elements
     */
    public static function rangeClosed(int startInclusive, int endInclusive): Stream<Int> {
        Iterator<Int> iterator = new RangeIterator(startInclusive, endInclusive + 1);
        return new StreamImpl<Int>(iterator);
    }

    /**
     * Creates a stream from an iterable source (any collection that implements Iterable).
     *
     * @param iterable the iterable source
     * @return a stream of elements from the iterable
     */
    public static function <T> fromIterable(Iterable<T> iterable): Stream<T> {
        Iterator<T> iterator = iterable.iterator();
        return new StreamImpl<T>(iterator);
    }
}

// ==================== Helper Iterator Implementations ====================

/**
 * Iterator that wraps an array for stream creation.
 */
class ArrayStreamIterator<T> implements Iterator<T> {
    private T[] array;
    private int index;

    constructor(T[] array) {
        this.array = array;
        this.index = 0;
    }

    public function hasNext(): bool {
        return this.index < this.array.length;
    }

    public function next(): T {
        if (this.index >= this.array.length) {
            throw new RuntimeException("No more elements in array stream");
        }
        T element = this.array[this.index];
        this.index = this.index + 1;
        return element;
    }

    public function close(): void {
        // Nothing to close for array iterator
    }
}

/**
 * Iterator that represents an empty stream.
 */
class EmptyIterator<T> implements Iterator<T> {
    constructor() {
        // Empty constructor
    }

    public function hasNext(): bool {
        return false;
    }

    public function next(): T {
        throw new RuntimeException("Empty iterator has no elements");
    }

    public function close(): void {
        // Nothing to close
    }
}

/**
 * Iterator that generates a range of integers.
 */
class RangeIterator implements Iterator<int> {
    private int current;
    private int end;

    constructor(int start, int end) {
        this.current = start;
        this.end = end;
    }

    public function hasNext(): bool {
        return this.current < this.end;
    }

    public function next(): int {
        if (this.current >= this.end) {
            throw "Range iterator exhausted";
        }
        int value = this.current;
        this.current = this.current + 1;
        return value;
    }

    public function close(): void {
        // Nothing to close
    }
}
