// Lifecycle + control annotations for mtest.
// @BeforeAll / @AfterAll must be applied to static methods.
// @Timeout.millis is parsed but not enforced in v1 (no timing native yet).
import * from "../../core/annotations/Retention.mt";
import * from "../../core/annotations/Targets.mt";

@Retention(RUNTIME)
@Target([METHOD])
annotation BeforeEach { }

@Retention(RUNTIME)
@Target([METHOD])
annotation AfterEach { }

@Retention(RUNTIME)
@Target([METHOD])
annotation BeforeAll { }

@Retention(RUNTIME)
@Target([METHOD])
annotation AfterAll { }

@Retention(RUNTIME)
@Target([METHOD])
annotation Disabled {
    string reason = "";
}

@Retention(RUNTIME)
@Target([METHOD])
annotation Timeout {
    int millis = 0;
}
