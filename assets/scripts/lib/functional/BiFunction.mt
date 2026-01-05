/**
 * Represents a function that accepts two arguments and produces a result.
 * Used for operations that need to combine two values of potentially different types.
 *
 * @param T the type of the first argument to the function
 * @param U the type of the second argument to the function
 * @param R the type of the result of the function
 *
 * Example:
 *   BiFunction<String, int, String> repeat = (String s, int n) => s.repeat(n);
 *   String result = repeat.apply("Hi", 3); // "HiHiHi"
 */
interface BiFunction<T, U, R> {
    /**
     * Applies this function to the given arguments.
     *
     * @param t the first function argument
     * @param u the second function argument
     * @return the function result
     */
    function apply(T t, U u): R;

    /**
     * Returns a composed function that first applies this function to
     * its input, and then applies the after function to the result.
     *
     * @param after the function to apply after this function is applied
     * @return a composed function that first applies this function and then
     *         applies the after function
     */
    function <V> andThen(Function<R, V> after): BiFunction<T, U, V>;
}
