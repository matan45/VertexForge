/**
 * Iterator that flattens nested streams produced by a mapping function.
 * Specialized version for Stream.flatMap() that works directly with Stream objects.
 *
 * @param T the type of input elements
 * @param R the type of output elements after mapping
 */
import * from "../Iterator.mt";
import * from "../functional/Function.mt";
import * from "../stream/Stream.mt";

class StreamFlatMappingIterator<T, R> implements Iterator<R> {
    Iterator<T> source;
    Function<T, Stream<R>> mapper;
    Iterator<R> currentInnerIterator;
    Stream<R> currentInnerStream;

    public constructor(Iterator<T> source, Function<T, Stream<R>> mapper) {
        this.source = source;
        this.mapper = mapper;
        this.currentInnerIterator = null;
        this.currentInnerStream = null;
    }

    public function hasNext(): bool {
        // If we have a current inner iterator with more elements, return true
        if (this.currentInnerIterator != null && this.currentInnerIterator.hasNext()) {
            return true;
        }

        // Try to find the next non-empty inner stream
        while (this.source.hasNext()) {
            T sourceElement = this.source.next();
            this.currentInnerStream = this.mapper.apply(sourceElement);

            // Convert stream to array and create iterator from it
            if (this.currentInnerStream != null) {
                R[] elements = this.currentInnerStream.toArray();
                if (elements.length > 0) {
                    this.currentInnerIterator = new ArrayIteratorForFlatMap<R>(elements);
                    return true;
                }
            }
        }

        return false;
    }

    public function next(): R {
        if (!this.hasNext()) {
            throw "No more elements in StreamFlatMappingIterator";
        }

        return this.currentInnerIterator.next();
    }

    public function close(): void {
        // Close the source iterator
        if (this.source != null) {
            this.source.close();
        }

        // Close current inner iterator if any
        if (this.currentInnerIterator != null) {
            this.currentInnerIterator.close();
        }
    }
}

/**
 * Simple array-backed iterator for flatMap results.
 */
class ArrayIteratorForFlatMap<R> implements Iterator<R> {
    private R[] array;
    private int index;

    constructor(R[] array) {
        this.array = array;
        this.index = 0;
    }

    public function hasNext(): bool {
        return this.index < this.array.length;
    }

    public function next(): R {
        if (this.index >= this.array.length) {
            throw "No more elements";
        }
        R element = this.array[this.index];
        this.index = this.index + 1;
        return element;
    }

    public function close(): void {
        // Nothing to close for array iterator
    }
}
