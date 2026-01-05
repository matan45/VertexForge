/**
 * Represents an operation that accepts a single input argument and returns no result.
 * Used for side-effect operations like forEach().
 *
 * @param T the type of the input to the operation
 *
 * Example:
 *   Consumer<String> printer = (String s) => print(s);
 *   printer.accept("Hello"); // prints "Hello"
 */
interface Consumer<T> {
    /**
     * Performs this operation on the given argument.
     *
     * @param t the input argument
     */
    function accept(T t): void;

    /**
     * Returns a composed Consumer that performs, in sequence, this
     * operation followed by the after operation.
     *
     * @param after the operation to perform after this operation
     * @return a composed Consumer that performs in sequence this
     *         operation followed by the after operation
     */
    function andThen(Consumer<T> after): Consumer<T>;
}
