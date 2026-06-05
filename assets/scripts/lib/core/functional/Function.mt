/**
 * Represents a function that accepts one argument and produces a result.
 * Single-method functional interface (SAM); a lambda may be assigned directly.
 *
 * @param T the type of the input to the function
 * @param R the type of the result of the function
 *
 * Example:
 *   Function<Int, Int> doubler = x -> new Int(x.getValue() * 2);
 *   Int result = doubler.apply(new Int(21)); // 42
 */
interface Function<T, R> {
    /**
     * Applies this function to the given argument.
     *
     * @param t the function argument
     * @return the function result
     */
    function apply(T t): R;
}
