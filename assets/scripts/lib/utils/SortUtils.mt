/**
 * Utility class for sorting arrays using various algorithms.
 * Provides in-place quicksort implementation.
 */
import * from "../functional/Comparator.mt";

class SortUtils {
    /**
     * Sorts an array using quicksort algorithm with a comparator.
     *
     * @param array the array to sort (modified in-place)
     * @param comparator the comparator to determine element order
     */
    public static function <T> quicksort(T[] array, Comparator<T> comparator): void {
        if (array == null) {
            return;
        }
        quicksortRange(array, 0, array.length - 1, comparator);
    }

    /**
     * Recursive quicksort implementation for a range of the array.
     */
    public static function <T> quicksortRange(T[] array, int low, int high, Comparator<T> comparator): void {
        if (low < high) {
            int pivotIndex = partition(array, low, high, comparator);
            quicksortRange(array, low, pivotIndex - 1, comparator);
            quicksortRange(array, pivotIndex + 1, high, comparator);
        }
    }

    /**
     * Partitions the array around a pivot element.
     * Returns the final position of the pivot.
     */
    private static function <T> partition(T[] array, int low, int high, Comparator<T> comparator): int {
        T pivot = array[high];
        int i = low - 1;

        for (int j = low; j < high; j++) {
            if (comparator.compare(array[j], pivot) <= 0) {
                i++;
                swap(array, i, j);
            }
        }

        swap(array, i + 1, high);
        return i + 1;
    }

    /**
     * Swaps two elements in an array.
     */
    private static function <T> swap(T[] array, int i, int j): void {
        T temp = array[i];
        array[i] = array[j];
        array[j] = temp;
    }

    /**
     * Sorts an array using natural ordering (for primitive types).
     * This is a simplified version that uses integer comparison.
     */
    public static function quicksortInt(int[] array): void {
        if (array == null) {
            return;
        }
        quicksortRangeInt(array, 0, array.length - 1);
    }

    private static function quicksortRangeInt(int[] array, int low, int high): void {
        if (low < high) {
            int pivotIndex = partitionInt(array, low, high);
            quicksortRangeInt(array, low, pivotIndex - 1);
            quicksortRangeInt(array, pivotIndex + 1, high);
        }
    }

    private static function partitionInt(int[] array, int low, int high): int {
        int pivot = array[high];
        int i = low - 1;

        for (int j = low; j < high; j++) {
            if (array[j] <= pivot) {
                i++;
                swapInt(array, i, j);
            }
        }

        swapInt(array, i + 1, high);
        return i + 1;
    }

    private static function swapInt(int[] array, int i, int j): void {
        int temp = array[i];
        array[i] = array[j];
        array[j] = temp;
    }
}
