/**
 * Interface for objects that can be compared for ordering.
 * Implementing this interface allows objects to be sorted using natural ordering.
 *
 * @param T the type of objects that this object may be compared to
 *
 * Example:
 *   class Person implements Comparable<Person> {
 *       int age;
 *       function compareTo(Person other): int {
 *           return this.age - other.age;
 *       }
 *   }
 */
interface Comparable<T> {
    /**
     * Compares this object with the specified object for order.
     * Returns a negative integer, zero, or a positive integer as this object
     * is less than, equal to, or greater than the specified object.
     *
     * @param other the object to be compared
     * @return a negative integer, zero, or a positive integer as this object
     *         is less than, equal to, or greater than the specified object
     */
    function compareTo(T other): int;
}
