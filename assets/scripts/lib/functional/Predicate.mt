/**
 * Represents a predicate (boolean-valued function) of one argument.
 * Used for filtering operations like filter().
 *
 * @param T the type of the input to the predicate
 *
 * Example:
 *   Predicate<int> isPositive = (int x) => x > 0;
 *   bool result = isPositive.test(5); // true
 */
interface Predicate<T> {
    /**
     * Evaluates this predicate on the given argument.
     *
     * @param t the input argument
     * @return true if the input argument matches the predicate, otherwise false
     */
    function test(T t): bool;
}
