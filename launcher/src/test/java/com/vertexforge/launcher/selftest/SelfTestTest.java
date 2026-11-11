package com.vertexforge.launcher.selftest;

import com.vertexforge.launcher.model.ProjectInfo;
import com.vertexforge.launcher.selftest.SelfTest.CheckFailure;
import com.vertexforge.launcher.selftest.SelfTest.Reporter;
import com.vertexforge.launcher.service.ProjectHub;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The self-test's UI part needs the JavaFX toolkit, so it only runs in the packaged image (CI). */
class SelfTestTest {

    @TempDir
    Path tempDir;

    @Test
    void theCoreChecksPassAndStayInsideTheWorkFolder() throws CheckFailure {
        ProjectHub hub = SelfTest.runCoreChecks(tempDir);

        List<ProjectInfo> projects = hub.listProjects();
        assertEquals(1, projects.size());
        assertTrue(projects.getFirst().path().startsWith(tempDir));
        assertTrue(Files.isRegularFile(tempDir.resolve("config/recent-projects.json")));
    }

    @Test
    void theHermeticEnvironmentNeverStartsAProcessOrReadsTheRealEnvironment() {
        var environment = SelfTest.hermeticEnvironment(tempDir);

        assertEquals(null, environment.env().apply("PATH"));
        assertEquals(tempDir.resolve("config"), environment.configDir());
        assertThrows(IOException.class, () -> environment.processStarter().start(new ProcessBuilder("cmd")));
    }

    @Test
    void theFlagIsRecognisedAnywhereInTheArguments() {
        assertTrue(SelfTest.isRequested(new String[]{"--self-test"}));
        assertTrue(SelfTest.isRequested(new String[]{"--self-test-report=r.txt", "--self-test"}));
        assertFalse(SelfTest.isRequested(new String[]{}));
        assertFalse(SelfTest.isRequested(new String[]{"Game.vfproj"}));
    }

    @Test
    void theReportPathIsOptional() {
        assertEquals(Optional.of(Path.of("out/report.txt")),
                SelfTest.reportPath(new String[]{"--self-test", "--self-test-report=out/report.txt"}));
        assertEquals(Optional.empty(), SelfTest.reportPath(new String[]{"--self-test"}));
        assertEquals(Optional.empty(), SelfTest.reportPath(new String[]{"--self-test", "--self-test-report="}));
    }

    @Test
    void aFailingCheckNamesItsStep() {
        IllegalStateException cause = new IllegalStateException("missing resource /css/styles.css");

        CheckFailure failure = assertThrows(CheckFailure.class, () -> SelfTest.check("resources", () -> {
            throw cause;
        }));

        assertEquals("resources", failure.step);
        assertSame(cause, failure.getCause());
    }

    @Test
    void aMissingClassAlsoFailsTheCheck() {
        CheckFailure failure = assertThrows(CheckFailure.class, () -> SelfTest.check("main.fxml", () -> {
            throw new NoClassDefFoundError("javafx/fxml/FXMLLoader");
        }));

        assertEquals("main.fxml", failure.step);
    }

    @Test
    void passGoesToStdoutAndTheReport() throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        ByteArrayOutputStream err = new ByteArrayOutputStream();
        Path report = tempDir.resolve("reports/self-test.txt");
        Reporter reporter = new Reporter(report, print(out), print(err));

        assertEquals(0, reporter.pass());

        assertEquals("SELF-TEST PASS" + System.lineSeparator(), out.toString(StandardCharsets.UTF_8));
        assertEquals("", err.toString(StandardCharsets.UTF_8));
        assertEquals("SELF-TEST PASS" + System.lineSeparator(), Files.readString(report));
    }

    @Test
    void failGoesToStderrAndTheReportWithTheWholeCauseChain() throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        ByteArrayOutputStream err = new ByteArrayOutputStream();
        Path report = tempDir.resolve("self-test.txt");
        Reporter reporter = new Reporter(report, print(out), print(err));
        Exception cause = new IOException("load failed", new IllegalStateException("Error resolving onAction='#x'"));

        assertEquals(1, reporter.fail("main.fxml", cause));

        String text = Files.readString(report);
        assertTrue(text.startsWith("SELF-TEST FAIL: main.fxml: java.io.IOException: load failed"
                + " | caused by java.lang.IllegalStateException: Error resolving onAction='#x'"), text);
        assertTrue(text.contains("Caused by: java.lang.IllegalStateException"), text);
        assertEquals(text, err.toString(StandardCharsets.UTF_8));
        assertEquals("", out.toString(StandardCharsets.UTF_8));
    }

    @Test
    void onlyTheFirstOutcomeCounts() {
        Reporter reporter = new Reporter(null, print(new ByteArrayOutputStream()), print(new ByteArrayOutputStream()));

        assertFalse(reporter.isDone());
        assertEquals(0, reporter.pass());
        assertEquals(0, reporter.fail("watchdog", new IllegalStateException("late")));
        assertEquals(0, reporter.exitCode());
    }

    private static PrintStream print(ByteArrayOutputStream bytes) {
        return new PrintStream(bytes, true, StandardCharsets.UTF_8);
    }
}
