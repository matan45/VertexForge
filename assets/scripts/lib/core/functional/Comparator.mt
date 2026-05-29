/**
 * A comparison function, which imposes a total ordering on some collection of objects.
 * Single-method functional interface (SAM); a lambda may be assigned directly.
 * Used for sorting operations like sorted().
 *
 * @param T the type of objects that may be compared by this comparator
 *
 * Example:
 *   Comparator<Int> ascending = (a, b) -> a.getValue() - b.getValue();
 *   int cmp = ascending.compare(new Int(3), new Int(5)); // -2
 */
interface Comparator<T> {
    /**
     * Compares its two arguments for order.
     * Returns a negative integer, zero, or a positive integer as the
     * first argument is less than, equal to, or greater than the second.
     *
     * @param o1 the first object to be compared
     * @param o2 the second object to be compared
     * @return a negative integer, zero, or a positive integer as the
     *         first argument is less than, equal to, or greater than the second
     */
    function compare(T o1, T o2): int;
}
