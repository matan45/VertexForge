/**
 * Represents an operation upon two operands of the same type, producing a result
 * of the same type as the operands.
 * Used for reduction operations like reduce().
 *
 * @param T the type of the operands and result of the operator
 *
 * Example:
 *   BinaryOperator<int> sum = (int a, int b) => a + b;
 *   int result = sum.apply(3, 5); // 8
 */
interface BinaryOperator<T> {
    /**
     * Applies this operator to the given operands.
     *
     * @param left the first operand
     * @param right the second operand
     * @return the operator result
     */
    function apply(T left, T right): T;
}
