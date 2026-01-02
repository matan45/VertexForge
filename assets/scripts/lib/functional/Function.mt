/**
 * Represents a function that accepts one argument and produces a result.
 * Used for transformational operations like map().
 *
 * @param T the type of the input to the function
 * @param R the type of the result of the function
 *
 * Example:
 *   Function<int, String> intToString = (int x) => String.valueOf(x);
 *   String result = intToString.apply(42); // "42"
 */
interface Function<T, R> {
    /**
     * Applies this function to the given argument.
     *
     * @param t the function argument
     * @return the function result
     */
    function apply(T t): R;

    /**
     * Returns a composed function that first applies this function to
     * its input, and then applies the after function to the result.
     *
     * @param after the function to apply after this function is applied
     * @return a composed function that first applies this function and then
     *         applies the after function
     */
    function <V> andThen(Function<R, V> after): Function<T, V>;

    /**
     * Returns a composed function that first applies the before function to
     * its input, and then applies this function to the result.
     *
     * @param before the function to apply before this function is applied
     * @return a composed function that first applies the before function and
     *         then applies this function
     */
    function <V> compose(Function<V, T> before): Function<V, R>;
}
