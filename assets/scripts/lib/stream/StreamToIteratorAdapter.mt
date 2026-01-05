/**
 * Adapter class that converts a Function<T, Stream<R>> to Function<T, Iterator<R>>.
 * Used internally by StreamImpl.flatMap() to bridge between Stream and Iterator APIs.
 *
 * @param T the input type
 * @param R the output element type
 */
import * from "../functional/Function.mt";
import * from "../Iterator.mt";
import * from "Stream.mt";
import * from "StreamImpl.mt";

class StreamToIteratorAdapter<T, R> implements Function<T, Iterator<R>> {
    private Function<T, Stream<R>> streamMapper;

    public constructor(Function<T, Stream<R>> streamMapper) {
        this.streamMapper = streamMapper;
    }

    /**
     * Applies the stream mapper and extracts the iterator.
     * Casts the Stream to StreamImpl to access the iterator() method.
     */
    public function apply(T t): Iterator<R> {
        Stream<R> stream = this.streamMapper.apply(t);
        // Cast to StreamImpl to access iterator() method
        StreamImpl<R> streamImpl = (StreamImpl<R>) stream;
        return streamImpl.iterator();
    }

    /**
     * Not implemented - throws exception if called.
     */
    public function <V> andThen(Function<Iterator<R>, V> after): Function<T, V> {
        throw "andThen not implemented for StreamToIteratorAdapter";
    }

    /**
     * Not implemented - throws exception if called.
     */
    public function <V> compose(Function<V, T> before): Function<V, Iterator<R>> {
        throw "compose not implemented for StreamToIteratorAdapter";
    }
}
