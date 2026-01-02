/**
 * Iterator that flattens nested iterators produced by a mapping function.
 * Used for implementing Stream.flatMap().
 *
 * @param T the type of input elements
 * @param R the type of output elements after mapping
 */
import * from "../Iterator.mt";
import * from "../functional/Function.mt";

class FlatMappingIterator<T, R> implements Iterator<R> {
    Iterator<T> source;
    Function<T, Iterator<R>> mapper;
    Iterator<R> currentInnerIterator;

    public constructor(Iterator<T> source, Function<T, Iterator<R>> mapper) {
        this.source = source;
        this.mapper = mapper;
        this.currentInnerIterator = null;
    }

    public function hasNext(): bool {
        // If we have a current inner iterator with more elements, return true
        if (this.currentInnerIterator != null && this.currentInnerIterator.hasNext()) {
            return true;
        }

        // Try to find the next non-empty inner iterator
        while (this.source.hasNext()) {
            T sourceElement = this.source.next();
            this.currentInnerIterator = this.mapper.apply(sourceElement);

            if (this.currentInnerIterator != null && this.currentInnerIterator.hasNext()) {
                return true;
            }
        }

        return false;
    }

    public function next(): R {
        if (!this.hasNext()) {
            throw "No more elements in FlatMappingIterator";
        }

        return this.currentInnerIterator.next();
    }

    public function toArray(): R[] {
        // Collect all remaining elements
        int initialCapacity = 16;
        R[] result = new R[initialCapacity];
        int count = 0;

        while (this.hasNext()) {
            if (count >= result.length) {
                // Resize array
                int newCapacity = result.length * 2;
                R[] newArray = new R[newCapacity];
                for (int i = 0; i < count; i++) {
                    newArray[i] = result[i];
                }
                result = newArray;
            }

            result[count] = this.next();
            count++;
        }

        // Trim to actual size
        R[] finalArray = new R[count];
        for (int i = 0; i < count; i++) {
            finalArray[i] = result[i];
        }

        return finalArray;
    }

    public function close(): void {
        // Close the source iterator
        if (this.source != null) {
            this.source.close();
        }

        // Close current inner iterator if any
        if (this.currentInnerIterator != null) {
            this.currentInnerIterator.close();
        }
    }
}
