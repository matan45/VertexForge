// IndexOutOfBoundsException - thrown when accessing invalid array index
import * from "RuntimeException.mt";

class IndexOutOfBoundsException extends RuntimeException {
    public int index;
    public int size;

    // Constructor with message
    public constructor(string msg) : super(msg) {
        index = -1;
        size = -1;
    }

    // Default constructor
    public constructor() : super("Index out of bounds") {
        index = -1;
        size = -1;
    }

    // Get the invalid index
    public function getIndex(): int {
        return index;
    }

    // Get the size
    public function getSize(): int {
        return size;
    }

    // Convert to string
    public function toString(): string {
        return "IndexOutOfBoundsException: " + message;
    }
}
