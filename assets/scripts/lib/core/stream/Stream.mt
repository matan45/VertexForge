import * from "../functional/Function.mt";
import * from "../functional/Predicate.mt";
import * from "../functional/Consumer.mt";
import * from "../functional/BinaryOperator.mt";
import * from "../functional/Comparator.mt";

/**
 * A sequence of elements supporting sequential aggregate operations.
 * Streams provide a functional-style API for processing collections with operations
 * like filter, map, reduce, etc.
 *
 * Streams are lazy - intermediate operations are not executed until a terminal
 * operation is invoked.
 *
 * @param T the type of the stream elements
 *
 * Example:
 *   Stream<int> stream = list.stream()
 *       .filter((int x) => x > 0)
 *       .map((int x) => x * 2)
 *       .sorted();
 *   int[] result = stream.toArray();
 */
interface Stream<T> {
    // ==================== Intermediate Operations ====================
    // These operations return a new Stream and are lazily evaluated

    /**
     * Returns a stream consisting of the elements of this stream that match
     * the given predicate.
     *
     * @param predicate a predicate to apply to each element to determine if it
     *                  should be included
     * @return the new stream
     */
    function filter(Predicate<T> predicate): Stream<T>;

    /**
     * Returns a stream consisting of the results of applying the given
     * function to the elements of this stream.
     *
     * @param mapper a function to apply to each element
     * @return the new stream
     */
    function <R> map(Function<T, R> mapper): Stream<R>;

    /**
     * Returns a stream consisting of the results of replacing each element of
     * this stream with the contents of a mapped stream produced by applying
     * the provided mapping function to each element.
     *
     * @param mapper a function to apply to each element which produces a stream
     *               of new values
     * @return the new stream
     */
    function <R> flatMap(Function<T, Stream<R>> mapper): Stream<R>;

    /**
     * Returns a stream consisting of the distinct elements (according to
     * Object.equals()) of this stream.
     *
     * @return the new stream
     */
    function distinct(): Stream<T>;

    /**
     * Returns a stream consisting of the elements of this stream, sorted
     * according to natural order.
     *
     * @return the new stream
     */
    function sorted(): Stream<T>;

    /**
     * Returns a stream consisting of the elements of this stream, sorted
     * according to the provided comparator.
     *
     * @param comparator a comparator to be used to compare stream elements
     * @return the new stream
     */
    function sortedWith(Comparator<T> comparator): Stream<T>;

    /**
     * Returns a stream consisting of the elements of this stream, truncated
     * to be no longer than maxSize in length.
     *
     * @param maxSize the number of elements the stream should be limited to
     * @return the new stream
     */
    function limit(int maxSize): Stream<T>;

    /**
     * Returns a stream consisting of the remaining elements of this stream
     * after discarding the first n elements.
     *
     * @param n the number of leading elements to skip
     * @return the new stream
     */
    function skip(int n): Stream<T>;

    /**
     * Returns a stream consisting of the elements of this stream, additionally
     * performing the provided action on each element as elements are consumed
     * from the resulting stream.
     *
     * @param action a non-interfering action to perform on the elements as
     *               they are consumed from the stream
     * @return the new stream
     */
    function peek(Consumer<T> action): Stream<T>;

    // ==================== Terminal Operations ====================
    // These operations trigger the stream pipeline execution

    /**
     * Performs an action for each element of this stream.
     *
     * @param action a non-interfering action to perform on the elements
     */
    function forEach(Consumer<T> action): void;

    /**
     * Returns an array containing the elements of this stream.
     *
     * @return an array containing the elements of this stream
     */
    function toArray(): T[];

    /**
     * Performs a reduction on the elements of this stream, using an
     * associative accumulation function, and returns the reduced value.
     *
     * @param accumulator an associative function for combining two values
     * @return the result of the reduction, or null if the stream is empty
     */
    function reduce(BinaryOperator<T> accumulator): T;

    /**
     * Performs a reduction on the elements of this stream, using the provided
     * identity value and an associative accumulation function, and returns the
     * reduced value.
     *
     * @param identity the identity value for the accumulating function
     * @param accumulator an associative function for combining two values
     * @return the result of the reduction
     */
    function reduceWithIdentity(T identity, BinaryOperator<T> accumulator): T;

    /**
     * Returns the count of elements in this stream.
     *
     * @return the count of elements in this stream
     */
    function count(): int;

    /**
     * Returns whether any elements of this stream match the provided predicate.
     *
     * @param predicate a predicate to apply to elements of this stream
     * @return true if any elements of the stream match the provided predicate,
     *         otherwise false
     */
    function anyMatch(Predicate<T> predicate): bool;

    /**
     * Returns whether all elements of this stream match the provided predicate.
     *
     * @param predicate a predicate to apply to elements of this stream
     * @return true if either all elements of the stream match the provided
     *         predicate or the stream is empty, otherwise false
     */
    function allMatch(Predicate<T> predicate): bool;

    /**
     * Returns whether no elements of this stream match the provided predicate.
     *
     * @param predicate a predicate to apply to elements of this stream
     * @return true if either no elements of the stream match the provided
     *         predicate or the stream is empty, otherwise false
     */
    function noneMatch(Predicate<T> predicate): bool;

    /**
     * Returns the minimum element of this stream according to the provided
     * comparator.
     *
     * @param comparator a comparator to compare elements of this stream
     * @return the minimum element of this stream, or null if the stream is empty
     */
    function min(Comparator<T> comparator): T;

    /**
     * Returns the maximum element of this stream according to the provided
     * comparator.
     *
     * @param comparator a comparator to compare elements of this stream
     * @return the maximum element of this stream, or null if the stream is empty
     */
    function max(Comparator<T> comparator): T;
}
