// TestRunner — drives discovery and execution of mtest suites.
//
// Usage:
//   TestRunner r = new TestRunner();
//   r.addClass("CalculatorTest");
//   await r.run();   // returns ArrayList<TestSuiteResult>
//
// Discovery is explicit: callers enumerate suite class names. mType lacks a
// "scan all loaded classes" native, so auto-discovery is a separate concern.
//
// Within a suite the runner partitions declared methods by annotation, then
// executes @BeforeAll, each @Test (with @BeforeEach / @AfterEach around it),
// then @AfterAll. AssertionFailedException is treated as FAIL with the raw
// message; any other Exception is FAIL with "unexpected: ..." unless
// @Test(expected=...) matched the thrown class name.
import * from "../core/collections/ArrayList.mt";
import * from "../core/primitives/Int.mt";
import * from "../core/primitives/String.mt";
import * from "../core/reflect/Class.mt";
import * from "../core/reflect/Method.mt";
import * from "../core/reflect/Constructor.mt";
import * from "../core/reflect/Annotation.mt";
import * from "../core/exceptions/Exception.mt";
import * from "./TestResult.mt";
import * from "./TestSuiteResult.mt";
import * from "./AssertionFailedException.mt";
import * from "./ExceptionName.mt";

public class TestRunner {
    private ArrayList<String> _classNames;
    private ArrayList<TestSuiteResult> _suiteResults;
    private bool _verbose;

    public constructor() {
        this._classNames = new ArrayList<String>();
        this._suiteResults = new ArrayList<TestSuiteResult>();
        this._verbose = false;
    }

    public function addClass(string className): TestRunner {
        this._classNames.add(new String(className));
        return this;
    }

    public function setVerbose(bool v): TestRunner {
        this._verbose = v;
        return this;
    }

    public function async run(): Promise<ArrayList<TestSuiteResult>> {
        int totalPassed = 0;
        int totalFailed = 0;
        int totalSkipped = 0;

        for (int i = 0; i < this._classNames.size(); i = i + 1) {
            string name = this._classNames.get(i).getValue();
            TestSuiteResult suiteResult = await this._runSuite(name);
            this._suiteResults.add(suiteResult);
            suiteResult.printSummary();
            totalPassed = totalPassed + suiteResult.getPassedCount();
            totalFailed = totalFailed + suiteResult.getFailedCount();
            totalSkipped = totalSkipped + suiteResult.getSkippedCount();
        }

        print("");
        print("=== mtest: " + totalPassed + " passed, " + totalFailed
              + " failed, " + totalSkipped + " skipped ===");
        if (totalFailed == 0) {
            print("ALL PASSED");
        } else {
            print("FAILED: " + totalFailed + " tests");
        }
        return this._suiteResults;
    }

    // Returns an exit-code style int (0 = all passed, 1 = any failure) so
    // callers can feed a CI gate. The runner does NOT terminate the process
    // — mType has no exit() native — the caller threads the code as needed.
    public function async runAndReturnExitCode(): Promise<Int> {
        await this.run();
        int failed = 0;
        for (int i = 0; i < this._suiteResults.size(); i = i + 1) {
            failed = failed + this._suiteResults.get(i).getFailedCount();
        }
        if (failed == 0) {
            return new Int(0);
        }
        return new Int(1);
    }

    function async _runSuite(string className): Promise<TestSuiteResult> {
        TestSuiteResult suiteResult = new TestSuiteResult(className);
        Class cls = Class::forName(className);
        Method[] allMethods = cls.getDeclaredMethods();

        Method[] beforeAll = this._collect(allMethods, "BeforeAll");
        Method[] afterAll  = this._collect(allMethods, "AfterAll");
        Method[] beforeEach = this._collect(allMethods, "BeforeEach");
        Method[] afterEach  = this._collect(allMethods, "AfterEach");
        Method[] tests      = this._collect(allMethods, "Test");

        // mType's Method.invoke requires a non-null Object for the receiver,
        // so "static" @BeforeAll / @AfterAll hooks are routed through a shared
        // bootstrap instance rather than a true static context. Any state
        // they set on `this` is visible only within hook runs, not to the
        // fresh per-test instances created below. Suites that need
        // shared-across-tests state should keep it in static fields (and
        // accept the v1 caveat that hooks see it via the bootstrap `this`).
        // Static-vs-instance enforcement is deliberately not performed —
        // see v1 limitations in Mtest.mt.
        Object bootstrap;
        try {
            bootstrap = this._newInstance(cls);
        } catch (Exception e) {
            for (int i = 0; i < tests.length; i = i + 1) {
                TestResult r = new TestResult(className, tests[i].getName());
                r.markFailed("suite constructor threw: " + e.getMessage());
                suiteResult.addResult(r);
            }
            return suiteResult;
        }

        String beforeAllFailure = await this._invokeAll(beforeAll, bootstrap);
        if (beforeAllFailure.getValue() != "") {
            for (int i = 0; i < tests.length; i = i + 1) {
                TestResult r = new TestResult(className, tests[i].getName());
                r.markFailed("BeforeAll failed: " + beforeAllFailure.getValue());
                suiteResult.addResult(r);
            }
            await this._invokeAll(afterAll, bootstrap);
            return suiteResult;
        }

        for (int t = 0; t < tests.length; t = t + 1) {
            TestResult result = await this._runOneTest(cls, tests[t], beforeEach, afterEach);
            suiteResult.addResult(result);
        }

        await this._invokeAll(afterAll, bootstrap);
        return suiteResult;
    }

    // Reflectively construct a fresh suite instance. Propagates any exception
    // the constructor or reflection lookup throws — callers wrap in try/catch.
    function _newInstance(Class cls): Object {
        Constructor ctor = cls.getDeclaredConstructor(0);
        Object[] ctorArgs = new Object[0];
        return ctor.newInstance(ctorArgs);
    }

    // Collect declared methods carrying `annName`, preserving declaration order.
    function _collect(Method[] methods, string annName): Method[] {
        int count = 0;
        for (int i = 0; i < methods.length; i = i + 1) {
            if (methods[i].hasAnnotation(annName)) {
                count = count + 1;
            }
        }
        Method[] out = new Method[count];
        int w = 0;
        for (int i = 0; i < methods.length; i = i + 1) {
            if (methods[i].hasAnnotation(annName)) {
                out[w] = methods[i];
                w = w + 1;
            }
        }
        return out;
    }

    // Invoke a set of hook methods in declaration order. Returns "" on full
    // success, or the first exception's message (prefixed with method name)
    // on failure so the caller can annotate dependent results.
    function async _invokeAll(Method[] hooks, Object instance): Promise<String> {
        for (int i = 0; i < hooks.length; i = i + 1) {
            Method m = hooks[i];
            try {
                Object[] noArgs = new Object[0];
                if (m.isAsync()) {
                    await m.invoke(instance, noArgs);
                } else {
                    m.invoke(instance, noArgs);
                }
            } catch (Exception e) {
                return new String(m.getName() + ": " + e.getMessage());
            }
        }
        return new String("");
    }

    function async _runOneTest(Class cls, Method testMethod, Method[] beforeEach, Method[] afterEach): Promise<TestResult> {
        TestResult result = new TestResult(cls.getName(), testMethod.getName());

        if (testMethod.hasAnnotation("Disabled")) {
            string reason = "";
            Annotation? dis = testMethod.getAnnotation("Disabled");
            if (dis != null) {
                reason = dis.getString("reason");
            }
            result.markSkipped(reason);
            return result;
        }

        Annotation? testAnn = testMethod.getAnnotation("Test");
        string expected = "";
        if (testAnn != null) {
            expected = testAnn.getString("expected");
        }
        result.setExpectedException(expected);

        if (testMethod.hasAnnotation("Timeout")) {
            Annotation? to = testMethod.getAnnotation("Timeout");
            if (to != null) {
                result.setDeclaredTimeoutMs(to.getInt("millis"));
            }
        }

        Object instance;
        try {
            instance = this._newInstance(cls);
        } catch (Exception e) {
            result.markFailed("suite constructor threw: " + e.getMessage());
            return result;
        }

        String beforeFailure = await this._invokeAll(beforeEach, instance);
        if (beforeFailure.getValue() != "") {
            result.markFailed("BeforeEach failed: " + beforeFailure.getValue());
            await this._invokeAll(afterEach, instance);
            return result;
        }

        bool threw = false;
        bool assertionFailure = false;
        string failureMsg = "";
        string actualExcName = "";
        try {
            Object[] noArgs = new Object[0];
            if (testMethod.isAsync()) {
                await testMethod.invoke(instance, noArgs);
            } else {
                testMethod.invoke(instance, noArgs);
            }
        } catch (AssertionFailedException afe) {
            threw = true;
            assertionFailure = true;
            failureMsg = afe.getMessage();
            actualExcName = "AssertionFailedException";
        } catch (Exception e) {
            threw = true;
            failureMsg = e.getMessage();
            actualExcName = ExceptionName::of(e);
        }

        String afterFailure = await this._invokeAll(afterEach, instance);

        if (assertionFailure) {
            result.markFailedWithActual(failureMsg, actualExcName);
        } else if (threw) {
            if (expected == "") {
                result.markFailedWithActual(
                    "unexpected " + actualExcName + ": " + failureMsg,
                    actualExcName);
            } else if (expected == "Exception" || actualExcName == expected) {
                result.markPassed();
            } else {
                result.markFailedWithActual(
                    "expected " + expected + " but got " + actualExcName + ": " + failureMsg,
                    actualExcName);
            }
        } else {
            if (expected != "") {
                result.markFailed("expected " + expected + " but nothing was thrown");
            } else {
                result.markPassed();
            }
        }

        if (afterFailure.getValue() != "" && result.isPassed()) {
            result.markFailed("AfterEach failed: " + afterFailure.getValue());
        }

        return result;
    }
}
