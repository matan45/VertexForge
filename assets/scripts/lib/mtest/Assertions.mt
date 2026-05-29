// Assertions — top-level assertion functions. Each failure throws
// AssertionFailedException; the TestRunner distinguishes this from arbitrary
// runtime exceptions so reporting is precise.
//
// v1 limitation: assertThrows compares the thrown exception's class name by
// parsing the prefix of its toString() (the `ClassName: ...` convention used
// across lib/exceptions/). Subclass matching is NOT handled — passing
// "Exception" matches any exception; any other name must match the override'd
// toString prefix exactly.
import * from "./AssertionFailedException.mt";
import * from "./ThrowingRunnable.mt";
import * from "./ExceptionName.mt";
import * from "../core/exceptions/Exception.mt";

// ----- boolean -----

public function assertTrue(bool condition): void {
    if (!condition) {
        throw new AssertionFailedException("expected true but was false");
    }
}

public function assertTrue(bool condition, string message): void {
    if (!condition) {
        throw new AssertionFailedException(message);
    }
}

public function assertFalse(bool condition): void {
    if (condition) {
        throw new AssertionFailedException("expected false but was true");
    }
}

public function assertFalse(bool condition, string message): void {
    if (condition) {
        throw new AssertionFailedException(message);
    }
}

// ----- equality: primitive overloads -----

public function assertEqual(int actual, int expected): void {
    if (actual != expected) {
        throw new AssertionFailedException(
            "expected " + expected + " but was " + actual);
    }
}

public function assertEqual(int actual, int expected, string message): void {
    if (actual != expected) {
        throw new AssertionFailedException(
            message + " (expected " + expected + " but was " + actual + ")");
    }
}

public function assertEqual(float actual, float expected): void {
    if (actual != expected) {
        throw new AssertionFailedException(
            "expected " + expected + " but was " + actual);
    }
}

public function assertEqual(float actual, float expected, string message): void {
    if (actual != expected) {
        throw new AssertionFailedException(
            message + " (expected " + expected + " but was " + actual + ")");
    }
}

public function assertEqual(bool actual, bool expected): void {
    if (actual != expected) {
        throw new AssertionFailedException(
            "expected " + expected + " but was " + actual);
    }
}

public function assertEqual(bool actual, bool expected, string message): void {
    if (actual != expected) {
        throw new AssertionFailedException(
            message + " (expected " + expected + " but was " + actual + ")");
    }
}

public function assertEqual(string actual, string expected): void {
    if (actual != expected) {
        throw new AssertionFailedException(
            "expected \"" + expected + "\" but was \"" + actual + "\"");
    }
}

public function assertEqual(string actual, string expected, string message): void {
    if (actual != expected) {
        throw new AssertionFailedException(
            message + " (expected \"" + expected + "\" but was \"" + actual + "\")");
    }
}

// ----- inequality -----

public function assertNotEqual(int actual, int other): void {
    if (actual == other) {
        throw new AssertionFailedException(
            "expected value different from " + other);
    }
}

public function assertNotEqual(float actual, float other): void {
    if (actual == other) {
        throw new AssertionFailedException(
            "expected value different from " + other);
    }
}

public function assertNotEqual(string actual, string other): void {
    if (actual == other) {
        throw new AssertionFailedException(
            "expected value different from \"" + other + "\"");
    }
}

public function assertNotEqual(bool actual, bool other): void {
    if (actual == other) {
        throw new AssertionFailedException(
            "expected value different from " + other);
    }
}

// ----- approximate float -----

public function assertApprox(float actual, float expected, float tolerance): void {
    float diff = actual - expected;
    if (diff < 0.0) {
        diff = -diff;
    }
    if (diff > tolerance) {
        throw new AssertionFailedException(
            "expected " + expected + " +/- " + tolerance
            + " but was " + actual);
    }
}

public function assertApprox(float actual, float expected, float tolerance, string message): void {
    float diff = actual - expected;
    if (diff < 0.0) {
        diff = -diff;
    }
    if (diff > tolerance) {
        throw new AssertionFailedException(
            message + " (expected " + expected + " +/- " + tolerance
            + " but was " + actual + ")");
    }
}

// ----- null checks -----

public function assertNull(Object value): void {
    if (value != null) {
        throw new AssertionFailedException("expected null but was non-null");
    }
}

public function assertNull(Object value, string message): void {
    if (value != null) {
        throw new AssertionFailedException(message);
    }
}

public function assertNotNull(Object value): void {
    if (value == null) {
        throw new AssertionFailedException("expected non-null but was null");
    }
}

public function assertNotNull(Object value, string message): void {
    if (value == null) {
        throw new AssertionFailedException(message);
    }
}

// ----- exception expectation -----
//
// `prefix` is prepended to the failure-message stem so the two public
// overloads can share the same core logic. Use "" for the no-message
// form; callers with a user message pass it + " " (with trailing space).
function _assertThrowsImpl(string expectedExceptionName, ThrowingRunnable body, string prefix): void {
    bool threw = false;
    string actualName = "";
    string actualMsg = "";
    try {
        body.run();
    } catch (Exception e) {
        threw = true;
        actualName = ExceptionName::of(e);
        actualMsg = e.getMessage();
    }
    if (!threw) {
        throw new AssertionFailedException(
            prefix + "expected exception " + expectedExceptionName
            + " but nothing was thrown");
    }
    if (expectedExceptionName == "Exception") {
        return;
    }
    if (actualName != expectedExceptionName) {
        throw new AssertionFailedException(
            prefix + "expected exception " + expectedExceptionName
            + " but got " + actualName + ": " + actualMsg);
    }
}

public function assertThrows(string expectedExceptionName, ThrowingRunnable body): void {
    _assertThrowsImpl(expectedExceptionName, body, "");
}

public function assertThrows(string expectedExceptionName, ThrowingRunnable body, string message): void {
    _assertThrowsImpl(expectedExceptionName, body, message + " — ");
}

// ----- unconditional failure -----

public function fail(string message): void {
    throw new AssertionFailedException(message);
}
