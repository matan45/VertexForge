// @Test — marks a method as a test case. Optional `expected` names the
// exception class the test body is expected to throw (empty = no expectation).
import * from "../../core/annotations/Retention.mt";
import * from "../../core/annotations/Targets.mt";

@Retention(RUNTIME)
@Target([METHOD])
annotation Test {
    string expected = "";
}
