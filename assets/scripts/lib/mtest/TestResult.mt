// TestResult — outcome for a single @Test method.
public class TestResult {
    // Status codes. Integer-valued because mType has no enum construct yet;
    // access via the is*()/mark*() helpers rather than literal comparisons.
    private static int STATUS_PENDING = 0;
    private static int STATUS_PASSED  = 1;
    private static int STATUS_FAILED  = 2;
    private static int STATUS_SKIPPED = 3;

    private string _className;
    private string _methodName;
    private int _status;
    private string _failureMessage;
    private string _skipReason;
    private string _expectedException;
    private string _actualException;
    private int _declaredTimeoutMs;

    public constructor(string className, string methodName) {
        this._className = className;
        this._methodName = methodName;
        this._status = TestResult::STATUS_PENDING;
        this._failureMessage = "";
        this._skipReason = "";
        this._expectedException = "";
        this._actualException = "";
        this._declaredTimeoutMs = 0;
    }

    public function getClassName(): string { return this._className; }
    public function getMethodName(): string { return this._methodName; }
    public function isPending(): bool { return this._status == TestResult::STATUS_PENDING; }
    public function isPassed(): bool  { return this._status == TestResult::STATUS_PASSED;  }
    public function isFailed(): bool  { return this._status == TestResult::STATUS_FAILED;  }
    public function isSkipped(): bool { return this._status == TestResult::STATUS_SKIPPED; }
    public function getFailureMessage(): string { return this._failureMessage; }
    public function getSkipReason(): string { return this._skipReason; }
    public function getExpectedException(): string { return this._expectedException; }
    public function getActualException(): string { return this._actualException; }
    public function getDeclaredTimeoutMs(): int { return this._declaredTimeoutMs; }

    public function setExpectedException(string name): void {
        this._expectedException = name;
    }

    public function setDeclaredTimeoutMs(int ms): void {
        this._declaredTimeoutMs = ms;
    }

    public function markPassed(): void {
        this._status = TestResult::STATUS_PASSED;
    }

    public function markFailed(string msg): void {
        this._status = TestResult::STATUS_FAILED;
        this._failureMessage = msg;
    }

    public function markFailedWithActual(string msg, string actualExcName): void {
        this._status = TestResult::STATUS_FAILED;
        this._failureMessage = msg;
        this._actualException = actualExcName;
    }

    public function markSkipped(string reason): void {
        this._status = TestResult::STATUS_SKIPPED;
        this._skipReason = reason;
    }

    public function formatLine(): string {
        string head = "[" + this._className + "] " + this._methodName;
        if (this.isPassed()) {
            string suffix = "";
            if (this._expectedException != "") {
                suffix = "  (expected=" + this._expectedException + ")";
            }
            if (this._declaredTimeoutMs > 0) {
                suffix = suffix + "  (timeout=" + this._declaredTimeoutMs + "ms unenforced)";
            }
            return head + " PASSED" + suffix;
        }
        if (this.isFailed()) {
            return head + " FAILED -- " + this._failureMessage;
        }
        if (this.isSkipped()) {
            if (this._skipReason == "") {
                return head + " SKIPPED";
            }
            return head + " SKIPPED (" + this._skipReason + ")";
        }
        return head + " PENDING";
    }
}
