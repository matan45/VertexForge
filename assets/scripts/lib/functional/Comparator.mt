/**
 * A comparison function, which imposes a total ordering on some collection of objects.
 * Used for sorting operations like sorted().
 *
 * @param T the type of objects that may be compared by this comparator
 *
 * Example:
 *   Comparator<int> ascending = (int a, int b) => a - b;
 *   int cmp = ascending.compare(3, 5); // -2 (negative means a < b)
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

    /**
     * Returns a comparator that imposes the reverse ordering of this comparator.
     *
     * @return a comparator that imposes the reverse ordering of this comparator
     */
    function reversed(): Comparator<T>;

    /**
     * Returns a lexicographic-order comparator with another comparator.
     * If this comparator considers two elements equal, the other comparator
     * is used to determine the order.
     *
     * @param other the other comparator to be used when this comparator
     *              compares two objects that are equal
     * @return a lexicographic-order comparator composed of this and then the
     *         other comparator
     */
    function thenComparing(Comparator<T> other): Comparator<T>;
}
