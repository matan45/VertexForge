// ThrowingRunnable — zero-arg functional interface used as the body argument
// to assertThrows. Any lambda `() -> { ... }` binds to this type.
public interface ThrowingRunnable {
    function run(): void;
}
