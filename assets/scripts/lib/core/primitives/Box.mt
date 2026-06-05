// Box<T> - Minimal generic wrapper used throughout the reflection test
// suite. Not a real primitive — lives next to Int/Float/Bool/String so
// reflection tests have a single canonical Box<T> to import instead of
// redefining one in every test file.

public class Box<T> {
    private T value;

    public constructor(T value) {
        this.value = value;
    }

    public function getValue(): T {
        return this.value;
    }

    public function setValue(T value): void {
        this.value = value;
    }
}
