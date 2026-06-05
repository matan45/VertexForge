// List<T> - Ordered collection with positional access

import * from "../Collection.mt";
import * from "../Iterator.mt";

interface List<T> extends Collection<T> {
    function get(int index): T;
    function set(int index, T item): void;
    function removeAt(int index): bool;
    function indexOf(T item): int;
    function lastIndexOf(T item): int;
    function first(): T;
    function last(): T;
    function reverse(): void;
}
