import * from "../Iterator.mt";
import * from "../functional/Function.mt";
import * from "../functional/Predicate.mt";
import * from "../functional/Consumer.mt";
import * from "../functional/BinaryOperator.mt";
import * from "../functional/Comparator.mt";
import * from "Stream.mt";
import * from "../exceptions/RuntimeException.mt";
import * from "../utils/SortUtils.mt";
import * from "../iterators/StreamFlatMappingIterator.mt";

/**
 * Internal implementation of the Stream interface.
 * Uses a lazy evaluation pipeline where operations are accumulated and only
 * executed when a terminal operation is invoked.
 *
 * @param T the type of stream elements
 */
class StreamImpl<T> implements Stream<T> {
    private Iterator<T> source;
    private bool consumed;

    /**
     * Creates a new stream from an iterator.
     *
     * @param iterator the source iterator
     */
    constructor(Iterator<T> iterator) {
        this.source = iterator;
        this.consumed = false;
    }

    /**
     * Checks if this stream has already been consumed by a terminal operation.
     * Streams can only be used once.
     */
    private function checkNotConsumed(): void {
        if (this.consumed) {
            throw new RuntimeException("Stream has already been operated upon or closed");
        }
    }

    // ==================== Intermediate Operations ====================

    public function filter(Predicate<T> predicate): Stream<T> {
        this.checkNotConsumed();
        Iterator<T> filteredIterator = new FilteringIterator<T>(this.source, predicate);
        return new StreamImpl<T>(filteredIterator);
    }

    public function <R> map(Function<T, R> mapper): Stream<R> {
        this.checkNotConsumed();
        Iterator<R> mappedIterator = new MappingIterator<T, R>(this.source, mapper);
        return new StreamImpl<R>(mappedIterator);
    }

    public function <R> flatMap(Function<T, Stream<R>> mapper): Stream<R> {
        this.checkNotConsumed();

        // Use StreamFlatMappingIterator for lazy evaluation
        // It works directly with Stream objects, converting them to arrays internally
        Iterator<R> flatIterator = new StreamFlatMappingIterator<T, R>(this.source, mapper);
        return new StreamImpl<R>(flatIterator);
    }

    public function distinct(): Stream<T> {
        this.checkNotConsumed();
        Iterator<T> distinctIterator = new DistinctIterator<T>(this.source);
        return new StreamImpl<T>(distinctIterator);
    }

    public function sorted(): Stream<T> {
        this.checkNotConsumed();
        // Natural ordering sort requires Comparable<T> interface
        // Since we can't enforce this at compile time, we throw an exception
        // Use sortedWith(Comparator) for custom sorting
        throw "Stream.sorted() requires elements to implement Comparable interface. Use sortedWith(Comparator) instead.";
    }

    public function sortedWith(Comparator<T> comparator): Stream<T> {
        this.checkNotConsumed();
        // Sorting requires materializing all elements
        T[] elements = this.toArray();
        this.consumed = true;

        // Perform sorting with comparator
        this.quickSort(elements, 0, elements.length - 1, comparator);

        // Create new iterator from sorted array
        Iterator<T> sortedIterator = new ArrayIteratorHelper<T>(elements);
        StreamImpl<T> result = new StreamImpl<T>(sortedIterator);
        result.consumed = false; // Allow the new stream to be used
        return result;
    }

    public function limit(int maxSize): Stream<T> {
        this.checkNotConsumed();
        Iterator<T> limitedIterator = new LimitingIterator<T>(this.source, maxSize);
        return new StreamImpl<T>(limitedIterator);
    }

    public function skip(int n): Stream<T> {
        this.checkNotConsumed();
        Iterator<T> skippedIterator = new SkippingIterator<T>(this.source, n);
        return new StreamImpl<T>(skippedIterator);
    }

    public function peek(Consumer<T> action): Stream<T> {
        this.checkNotConsumed();
        Iterator<T> peekingIterator = new PeekingIterator<T>(this.source, action);
        return new StreamImpl<T>(peekingIterator);
    }

    // ==================== Terminal Operations ====================

    public function forEach(Consumer<T> action): void {
        this.checkNotConsumed();
        this.consumed = true;

        while (this.source.hasNext()) {
            T element = this.source.next();
            action.accept(element);
        }

        this.source.close();
    }

    public function toArray(): T[] {
        this.checkNotConsumed();
        this.consumed = true;

        // Collect all elements into a temporary list-like structure
        // We'll use a simple array with growth strategy
        T[] result = new T[16]; // Initial capacity
        int size = 0;

        while (this.source.hasNext()) {
            if (size >= result.length) {
                // Grow the array
                T[] newResult = new T[result.length * 2];
                int i = 0;
                while (i < size) {
                    newResult[i] = result[i];
                    i = i + 1;
                }
                result = newResult;
            }

            result[size] = this.source.next();
            size = size + 1;
        }

        this.source.close();

        // Trim to actual size
        T[] trimmed = new T[size];
        int i = 0;
        while (i < size) {
            trimmed[i] = result[i];
            i = i + 1;
        }

        return trimmed;
    }

    public function reduce(BinaryOperator<T> accumulator): T {
        this.checkNotConsumed();
        this.consumed = true;

        if (!this.source.hasNext()) {
            this.source.close();
            return null;
        }

        T result = this.source.next();

        while (this.source.hasNext()) {
            T element = this.source.next();
            result = accumulator.apply(result, element);
        }

        this.source.close();
        return result;
    }

    public function reduceWithIdentity(T identity, BinaryOperator<T> accumulator): T {
        this.checkNotConsumed();
        this.consumed = true;

        T result = identity;

        while (this.source.hasNext()) {
            T element = this.source.next();
            result = accumulator.apply(result, element);
        }

        this.source.close();
        return result;
    }

    public function count(): int {
        this.checkNotConsumed();
        this.consumed = true;

        int count = 0;

        while (this.source.hasNext()) {
            this.source.next();
            count = count + 1;
        }

        this.source.close();
        return count;
    }

    public function anyMatch(Predicate<T> predicate): bool {
        this.checkNotConsumed();
        this.consumed = true;

        while (this.source.hasNext()) {
            if (predicate.test(this.source.next())) {
                this.source.close();
                return true;
            }
        }

        this.source.close();
        return false;
    }

    public function allMatch(Predicate<T> predicate): bool {
        this.checkNotConsumed();
        this.consumed = true;

        while (this.source.hasNext()) {
            if (!predicate.test(this.source.next())) {
                this.source.close();
                return false;
            }
        }

        this.source.close();
        return true;
    }

    public function noneMatch(Predicate<T> predicate): bool {
        this.checkNotConsumed();
        this.consumed = true;

        while (this.source.hasNext()) {
            if (predicate.test(this.source.next())) {
                this.source.close();
                return false;
            }
        }

        this.source.close();
        return true;
    }

    public function min(Comparator<T> comparator): T {
        this.checkNotConsumed();
        this.consumed = true;

        if (!this.source.hasNext()) {
            this.source.close();
            return null;
        }

        T minElement = this.source.next();

        while (this.source.hasNext()) {
            T element = this.source.next();
            if (comparator.compare(element, minElement) < 0) {
                minElement = element;
            }
        }

        this.source.close();
        return minElement;
    }

    public function max(Comparator<T> comparator): T {
        this.checkNotConsumed();
        this.consumed = true;

        if (!this.source.hasNext()) {
            this.source.close();
            return null;
        }

        T maxElement = this.source.next();

        while (this.source.hasNext()) {
            T element = this.source.next();
            if (comparator.compare(element, maxElement) > 0) {
                maxElement = element;
            }
        }

        this.source.close();
        return maxElement;
    }

    // ==================== Helper Methods ====================

    /**
     * QuickSort implementation for sortedWith()
     */
    private function quickSort(T[] arr, int low, int high, Comparator<T> comparator): void {
        if (low < high) {
            int pivotIndex = this.partition(arr, low, high, comparator);
            this.quickSort(arr, low, pivotIndex - 1, comparator);
            this.quickSort(arr, pivotIndex + 1, high, comparator);
        }
    }

    private function partition(T[] arr, int low, int high, Comparator<T> comparator): int {
        T pivot = arr[high];
        int i = low - 1;

        int j = low;
        while (j < high) {
            if (comparator.compare(arr[j], pivot) <= 0) {
                i = i + 1;
                // Swap arr[i] and arr[j]
                T temp = arr[i];
                arr[i] = arr[j];
                arr[j] = temp;
            }
            j = j + 1;
        }

        // Swap arr[i+1] and arr[high]
        T temp = arr[i + 1];
        arr[i + 1] = arr[high];
        arr[high] = temp;

        return i + 1;
    }
}

// ==================== Specialized Iterator Implementations ====================

/**
 * Iterator that filters elements based on a predicate.
 */
class FilteringIterator<T> implements Iterator<T> {
    private Iterator<T> source;
    private Predicate<T> predicate;
    private T nextElement;
    private bool hasNextElement;
    private bool initialized;

    constructor(Iterator<T> source, Predicate<T> predicate) {
        this.source = source;
        this.predicate = predicate;
        this.initialized = false;
    }

    private function advance(): void {
        this.hasNextElement = false;
        while (this.source.hasNext()) {
            T candidate = this.source.next();
            if (this.predicate.test(candidate)) {
                this.nextElement = candidate;
                this.hasNextElement = true;
                return;
            }
        }
    }

    public function hasNext(): bool {
        if (!this.initialized) {
            this.advance();
            this.initialized = true;
        }
        return this.hasNextElement;
    }

    public function next(): T {
        if (!this.initialized) {
            this.advance();
            this.initialized = true;
        }

        if (!this.hasNextElement) {
            throw "No more elements";
        }

        T result = this.nextElement;
        this.advance();
        return result;
    }

    public function close(): void {
        this.source.close();
    }
}

/**
 * Iterator that maps elements using a function.
 */
class MappingIterator<T, R> implements Iterator<R> {
    private Iterator<T> source;
    private Function<T, R> mapper;

    constructor(Iterator<T> source, Function<T, R> mapper) {
        this.source = source;
        this.mapper = mapper;
    }

    public function hasNext(): bool {
        return this.source.hasNext();
    }

    public function next(): R {
        T element = this.source.next();
        return this.mapper.apply(element);
    }

    public function close(): void {
        this.source.close();
    }
}

/**
 * Iterator that skips duplicate elements.
 */
class DistinctIterator<T> implements Iterator<T> {
    private Iterator<T> source;
    private T[] seen;
    private int seenCount;
    private T nextElement;
    private bool hasNextElement;
    private bool initialized;

    constructor(Iterator<T> source) {
        this.source = source;
        this.seen = new T[16];
        this.seenCount = 0;
        this.initialized = false;
    }

    private function contains(T element): bool {
        int i = 0;
        while (i < this.seenCount) {
            if (this.seen[i] == element) {
                return true;
            }
            i = i + 1;
        }
        return false;
    }

    private function addToSeen(T element): void {
        if (this.seenCount >= this.seen.length) {
            // Grow array
            T[] newSeen = new T[this.seen.length * 2];
            int i = 0;
            while (i < this.seenCount) {
                newSeen[i] = this.seen[i];
                i = i + 1;
            }
            this.seen = newSeen;
        }
        this.seen[this.seenCount] = element;
        this.seenCount = this.seenCount + 1;
    }

    private function advance(): void {
        this.hasNextElement = false;
        while (this.source.hasNext()) {
            T candidate = this.source.next();
            if (!this.contains(candidate)) {
                this.addToSeen(candidate);
                this.nextElement = candidate;
                this.hasNextElement = true;
                return;
            }
        }
    }

    public function hasNext(): bool {
        if (!this.initialized) {
            this.advance();
            this.initialized = true;
        }
        return this.hasNextElement;
    }

    public function next(): T {
        if (!this.initialized) {
            this.advance();
            this.initialized = true;
        }

        if (!this.hasNextElement) {
            throw "No more elements";
        }

        T result = this.nextElement;
        this.advance();
        return result;
    }

    public function close(): void {
        this.source.close();
    }
}

/**
 * Iterator that limits the number of elements.
 */
class LimitingIterator<T> implements Iterator<T> {
    private Iterator<T> source;
    private int maxSize;
    private int count;

    constructor(Iterator<T> source, int maxSize) {
        this.source = source;
        this.maxSize = maxSize;
        this.count = 0;
    }

    public function hasNext(): bool {
        return this.count < this.maxSize && this.source.hasNext();
    }

    public function next(): T {
        if (this.count >= this.maxSize) {
            throw "Limit exceeded";
        }
        this.count = this.count + 1;
        return this.source.next();
    }

    public function close(): void {
        this.source.close();
    }
}

/**
 * Iterator that skips the first n elements.
 */
class SkippingIterator<T> implements Iterator<T> {
    private Iterator<T> source;
    private int skipCount;
    private bool skipped;

    constructor(Iterator<T> source, int skipCount) {
        this.source = source;
        this.skipCount = skipCount;
        this.skipped = false;
    }

    private function performSkip(): void {
        int i = 0;
        while (i < this.skipCount && this.source.hasNext()) {
            this.source.next();
            i = i + 1;
        }
        this.skipped = true;
    }

    public function hasNext(): bool {
        if (!this.skipped) {
            this.performSkip();
        }
        return this.source.hasNext();
    }

    public function next(): T {
        if (!this.skipped) {
            this.performSkip();
        }
        return this.source.next();
    }

    public function close(): void {
        this.source.close();
    }
}

/**
 * Iterator that performs an action on each element as it passes through.
 */
class PeekingIterator<T> implements Iterator<T> {
    private Iterator<T> source;
    private Consumer<T> action;

    constructor(Iterator<T> source, Consumer<T> action) {
        this.source = source;
        this.action = action;
    }

    public function hasNext(): bool {
        return this.source.hasNext();
    }

    public function next(): T {
        T element = this.source.next();
        this.action.accept(element);
        return element;
    }

    public function close(): void {
        this.source.close();
    }
}

/**
 * Simple array-backed iterator helper for sorted results.
 */
class ArrayIteratorHelper<T> implements Iterator<T> {
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
            throw "No more elements";
        }
        T element = this.array[this.index];
        this.index = this.index + 1;
        return element;
    }

    public function close(): void {
        // Nothing to close for array iterator
    }
}
