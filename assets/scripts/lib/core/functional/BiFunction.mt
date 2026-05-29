/**
 * Represents a function that accepts two arguments and produces a result.
 * Single-method functional interface (SAM); a lambda may be assigned directly.
 *
 * @param T the type of the first argument to the function
 * @param U the type of the second argument to the function
 * @param R the type of the result of the function
 *
 * Example:
 *   BiFunction<String, Int, String> repeat = (s, n) -> s.repeat(n.getValue());
 *   String result = repeat.apply(new String("Hi"), new Int(3)); // "HiHiHi"
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
}
