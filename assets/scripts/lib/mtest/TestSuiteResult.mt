// TestSuiteResult — aggregate outcome for one test class. Owns an ArrayList
// of TestResult and tracks pass/fail/skip counters incrementally.
import * from "../core/collections/ArrayList.mt";
import * from "./TestResult.mt";

public class TestSuiteResult {
    private string _className;
    private ArrayList<TestResult> _results;
    private int _passed;
    private int _failed;
    private int _skipped;

    public constructor(string className) {
        this._className = className;
        this._results = new ArrayList<TestResult>();
        this._passed = 0;
        this._failed = 0;
        this._skipped = 0;
    }

    public function getClassName(): string { return this._className; }
    public function getResults(): ArrayList<TestResult> { return this._results; }
    public function getPassedCount(): int { return this._passed; }
    public function getFailedCount(): int { return this._failed; }
    public function getSkippedCount(): int { return this._skipped; }
    public function getTotal(): int {
        return this._passed + this._failed + this._skipped;
    }

    public function addResult(TestResult r): void {
        this._results.add(r);
        if (r.isPassed()) {
            this._passed = this._passed + 1;
        } else if (r.isFailed()) {
            this._failed = this._failed + 1;
        } else if (r.isSkipped()) {
            this._skipped = this._skipped + 1;
        }
    }

    public function printSummary(): void {
        for (int i = 0; i < this._results.size(); i = i + 1) {
            print(this._results.get(i).formatLine());
        }
        print("--- " + this._className + ": " + this._passed + " passed, "
              + this._failed + " failed, " + this._skipped + " skipped ("
              + this.getTotal() + " total) ---");
    }
}
