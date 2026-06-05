/**
 * Represents an operation that accepts a single input argument and returns no result.
 * Single-method functional interface (SAM); a lambda may be assigned directly.
 * Used for side-effect operations like forEach().
 *
 * @param T the type of the input to the operation
 *
 * Example:
 *   Consumer<String> printer = s -> print(s.getValue());
 *   printer.accept(new String("Hello")); // prints "Hello"
 */
interface Consumer<T> {
    /**
     * Performs this operation on the given argument.
     *
     * @param t the input argument
     */
    function accept(T t): void;
}
