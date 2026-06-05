// TestSuite — marker base class for mtest test classes. Subclass it so the
// runner knows a class is a suite; hook methods are still discovered via
// @BeforeEach / @AfterEach / @BeforeAll / @AfterAll annotations, not by name.
// The empty no-arg constructor lets TestRunner instantiate subclasses
// reflectively (fresh instance per test).
//
// Lifecycle note: TestRunner creates a *fresh* instance for every @Test,
// but @BeforeAll / @AfterAll hooks run on a single shared "bootstrap"
// instance (mType's Method.invoke currently needs a non-null receiver, so
// even static-intent hooks need one). Mutations to `this` inside a
// @BeforeAll hook are NOT visible to any @Test instance. For state that
// must persist across tests, use static fields — and remember the hook
// sees them via the bootstrap `this`.
public class TestSuite {
    public constructor() { }
}
