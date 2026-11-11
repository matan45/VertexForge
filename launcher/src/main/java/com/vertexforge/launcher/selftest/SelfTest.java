package com.vertexforge.launcher.selftest;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.vertexforge.launcher.env.LauncherEnvironment;
import com.vertexforge.launcher.model.ProjectCreationResult;
import com.vertexforge.launcher.model.ProjectInfo;
import com.vertexforge.launcher.model.RecentProjectsData;
import com.vertexforge.launcher.model.RecentProjectsData.ProjectData;
import com.vertexforge.launcher.service.ProjectCreator;
import com.vertexforge.launcher.service.ProjectHub;
import com.vertexforge.launcher.ui.MainView;
import com.vertexforge.launcher.util.LauncherLog;
import com.vertexforge.launcher.util.ZipExtractor;
import javafx.application.Platform;
import javafx.collections.ObservableList;
import javafx.css.CssParser;
import javafx.css.Stylesheet;
import javafx.scene.Parent;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.PrintStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Clock;
import java.time.Duration;
import java.util.Arrays;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.stream.Stream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/**
 * {@code --self-test}: proves that a build, above all the packaged app image, has every class, module and resource
 * the launcher needs (VK-1677). It starts the JavaFX toolkit, loads the real main window and stylesheet, runs a Gson
 * and a zip round trip and creates a project from the bundled templates, all inside a temp folder: it never shows a
 * window, never starts a process and never touches {@code ~/.vertexforge}.
 * <p>
 * Exit code 0 means pass and 1 means fail. {@code SELF-TEST PASS} goes to stdout and the failure reason to stderr;
 * with {@code --self-test-report=<file>} both also go to that file, which is the reliable channel for the packaged
 * exe (it has no console).
 */
public final class SelfTest {

    public static final String FLAG = "--self-test";
    public static final String REPORT_PREFIX = "--self-test-report=";
    static final Duration UI_TIMEOUT = Duration.ofSeconds(45);
    static final Duration WATCHDOG_TIMEOUT = Duration.ofSeconds(60);
    static final String PASS = "SELF-TEST PASS";
    static final String FAIL = "SELF-TEST FAIL";

    private static final List<String> REQUIRED_RESOURCES = List.of(
            MainView.FXML_RESOURCE,
            MainView.STYLESHEET_RESOURCE,
            "/images/icon.png",
            ProjectCreator.SCENE_TEMPLATE_RESOURCE,
            ProjectCreator.LIBRARY_ZIP_RESOURCE);

    private SelfTest() {
        // Utility class - prevent instantiation
    }

    public static boolean isRequested(String[] args) {
        return Arrays.asList(args).contains(FLAG);
    }

    /** The {@code --self-test-report=<file>} path, if given. */
    static Optional<Path> reportPath(String[] args) {
        for (String arg : args) {
            if (arg.startsWith(REPORT_PREFIX) && arg.length() > REPORT_PREFIX.length()) {
                return Optional.of(Path.of(arg.substring(REPORT_PREFIX.length())));
            }
        }
        return Optional.empty();
    }

    /** Runs every check and returns the process exit code. Never shows a window. */
    public static int run(String[] args) {
        Reporter reporter;
        try {
            reporter = new Reporter(reportPath(args).orElse(null), System.out, System.err);
        } catch (RuntimeException e) {
            System.err.println(FAIL + ": arguments: " + e);
            return 1;
        }

        Thread watchdog = startWatchdog(reporter);
        Path workDir = null;
        boolean toolkitStarted = false;
        try {
            workDir = Files.createTempDirectory("vf-launcher-selftest");
            ProjectHub hub = runCoreChecks(workDir);
            toolkitStarted = startToolkit();
            runUiChecks(hub);
            return reporter.pass();
        } catch (CheckFailure failure) {
            return reporter.fail(failure.step, failure.getCause());
        } catch (Throwable t) {
            return reporter.fail("setup", t);
        } finally {
            if (toolkitStarted) {
                Platform.exit();
            }
            deleteQuietly(workDir);
            watchdog.interrupt();
        }
    }

    /**
     * The checks that need no JavaFX toolkit (the unit tests run these too). Returns a hub over {@code workDir}
     * that already lists the project the checks created.
     */
    static ProjectHub runCoreChecks(Path workDir) throws CheckFailure {
        LauncherEnvironment environment = hermeticEnvironment(workDir);
        LauncherLog log = new LauncherLog(environment.configDir().resolve(LauncherLog.FILE_NAME), environment.clock(),
                LauncherLog.DEFAULT_MAX_BYTES, LauncherLog.DEFAULT_BACKUPS, System.err);
        ProjectHub hub = ProjectHub.create(environment, log);

        check("resources", SelfTest::checkResources);
        check("gson round trip", SelfTest::checkGsonRoundTrip);
        check("zip round trip", () -> checkZipRoundTrip(workDir.resolve("zip")));
        check("project creation", () -> checkProjectCreation(hub, workDir.resolve("projects")));
        return hub;
    }

    /** No user folder, no environment variables, no processes: everything lives below {@code workDir}. */
    static LauncherEnvironment hermeticEnvironment(Path workDir) {
        return new LauncherEnvironment(
                Clock.systemDefaultZone(),
                workDir.resolve("config"),
                workDir,
                name -> null,
                builder -> {
                    throw new IOException("the self-test never starts processes");
                });
    }

    private static void checkResources() throws IOException {
        for (String resource : REQUIRED_RESOURCES) {
            if (SelfTest.class.getResource(resource) == null) {
                throw new IllegalStateException("missing resource " + resource);
            }
        }
        try (InputStream in = SelfTest.class.getResourceAsStream(ProjectCreator.LIBRARY_ZIP_RESOURCE);
             ZipInputStream zip = new ZipInputStream(in)) {
            int files = 0;
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                if (!entry.isDirectory()) {
                    files++;
                }
            }
            if (files == 0) {
                throw new IllegalStateException(ProjectCreator.LIBRARY_ZIP_RESOURCE + " holds no files");
            }
        }
    }

    private static void checkGsonRoundTrip() {
        RecentProjectsData data = new RecentProjectsData(RecentProjectsData.CURRENT_VERSION, List.of(
                new ProjectData("Self Test", "C:\\Projects\\Self Test\\Self Test.vfproj", "2026-01-02T03:04:05Z")));
        Gson gson = new GsonBuilder().setPrettyPrinting().create();

        String json = gson.toJson(data);
        RecentProjectsData back = gson.fromJson(json, RecentProjectsData.class);

        if (!data.equals(back)) {
            throw new IllegalStateException("Gson round trip changed the data: " + json);
        }
    }

    private static void checkZipRoundTrip(Path targetDir) throws IOException {
        Map<String, byte[]> files = new LinkedHashMap<>();
        files.put("a.txt", "alpha".getBytes(StandardCharsets.UTF_8));
        files.put("nested/b.mt", "beta\r\n".getBytes(StandardCharsets.UTF_8));

        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            for (Map.Entry<String, byte[]> file : files.entrySet()) {
                zip.putNextEntry(new ZipEntry(file.getKey()));
                zip.write(file.getValue());
                zip.closeEntry();
            }
        }
        Files.createDirectories(targetDir);
        ZipExtractor.extract(new ByteArrayInputStream(bytes.toByteArray()), targetDir);

        for (Map.Entry<String, byte[]> file : files.entrySet()) {
            if (!Arrays.equals(file.getValue(), Files.readAllBytes(targetDir.resolve(file.getKey())))) {
                throw new IllegalStateException("zip round trip changed " + file.getKey());
            }
        }
    }

    private static void checkProjectCreation(ProjectHub hub, Path projectsDir) throws IOException {
        Files.createDirectories(projectsDir);
        ProjectCreationResult result = hub.createProject("SelfTest", projectsDir);
        if (!result.success()) {
            throw new IllegalStateException("project creation failed: " + result.errorMessage());
        }

        List<ProjectInfo> listed = hub.listProjects();
        if (listed.size() != 1 || !listed.getFirst().path().equals(result.projectPath())) {
            throw new IllegalStateException("the recent list did not round-trip the new project: " + listed);
        }

        try (Stream<Path> lib = Files.walk(result.projectPath().resolveSibling("scripts").resolve("lib"))) {
            if (lib.noneMatch(Files::isRegularFile)) {
                throw new IllegalStateException("the script library was not extracted");
            }
        }
    }

    private static boolean startToolkit() throws CheckFailure {
        check("JavaFX toolkit start", () -> {
            Platform.setImplicitExit(false);
            CountDownLatch started = new CountDownLatch(1);
            Platform.startup(started::countDown);
            if (!started.await(UI_TIMEOUT.toSeconds(), TimeUnit.SECONDS)) {
                throw new TimeoutException("the JavaFX toolkit did not start within " + UI_TIMEOUT.toSeconds() + " s");
            }
        });
        return true;
    }

    /** Loads the stylesheet and the real main window on the FX thread. */
    private static void runUiChecks(ProjectHub hub) throws CheckFailure {
        CompletableFuture<Void> done = new CompletableFuture<>();
        Platform.runLater(() -> {
            try {
                uiChecks(hub);
                done.complete(null);
            } catch (Throwable t) {
                done.completeExceptionally(t);
            }
        });

        try {
            done.get(UI_TIMEOUT.toSeconds(), TimeUnit.SECONDS);
        } catch (ExecutionException e) {
            if (e.getCause() instanceof CheckFailure failure) {
                throw failure;
            }
            throw new CheckFailure("main window", e.getCause());
        } catch (TimeoutException e) {
            throw new CheckFailure("main window",
                    new TimeoutException("the UI checks did not finish within " + UI_TIMEOUT.toSeconds() + " s"));
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new CheckFailure("main window", e);
        }
    }

    private static void uiChecks(ProjectHub hub) throws CheckFailure {
        // The error list only collects while it exists, so create it before anything parses CSS.
        ObservableList<CssParser.ParseError> cssErrors = CssParser.errorsProperty();
        cssErrors.clear();

        check("styles.css", () -> {
            Stylesheet stylesheet = new CssParser().parse(MainView.stylesheet());
            if (stylesheet.getRules().isEmpty()) {
                throw new IllegalStateException(MainView.STYLESHEET_RESOURCE + " has no rules");
            }
            requireNoCssErrors(cssErrors);
        });

        Parent[] root = new Parent[1];
        check("main.fxml", () -> root[0] = MainView.load(hub));

        check("main window scene", () -> {
            MainView.createScene(root[0]);
            root[0].applyCss();
            requireNoCssErrors(cssErrors);
        });
    }

    private static void requireNoCssErrors(List<CssParser.ParseError> errors) {
        if (!errors.isEmpty()) {
            throw new IllegalStateException("CSS errors: " + errors);
        }
    }

    private static Thread startWatchdog(Reporter reporter) {
        Thread watchdog = new Thread(() -> {
            try {
                Thread.sleep(WATCHDOG_TIMEOUT.toMillis());
            } catch (InterruptedException e) {
                return;
            }
            int code = reporter.isDone()
                    ? reporter.exitCode()
                    : reporter.fail("watchdog", new TimeoutException(
                            "the self-test did not finish within " + WATCHDOG_TIMEOUT.toSeconds() + " s"));
            Runtime.getRuntime().halt(code);
        }, "self-test-watchdog");
        watchdog.setDaemon(true);
        watchdog.start();
        return watchdog;
    }

    private static void deleteQuietly(Path dir) {
        if (dir == null || !Files.exists(dir)) {
            return;
        }
        try (Stream<Path> paths = Files.walk(dir)) {
            paths.sorted(Comparator.reverseOrder()).forEach(path -> {
                try {
                    Files.deleteIfExists(path);
                } catch (IOException ignored) {
                    // A leftover temp folder is harmless.
                }
            });
        } catch (IOException ignored) {
            // A leftover temp folder is harmless.
        }
    }

    @FunctionalInterface
    interface Check {
        void run() throws Exception;
    }

    /** Runs one named check; any throwable, including a missing class or module, fails that step. */
    static void check(String step, Check check) throws CheckFailure {
        try {
            check.run();
        } catch (CheckFailure failure) {
            throw failure;
        } catch (Throwable t) {
            throw new CheckFailure(step, t);
        }
    }

    static final class CheckFailure extends Exception {
        final String step;

        CheckFailure(String step, Throwable cause) {
            super(step + ": " + cause, cause);
            this.step = step;
        }
    }

    /** Writes the outcome once: PASS to stdout, FAIL to stderr, and both to the optional report file. */
    static final class Reporter {
        private final Path reportFile;
        private final PrintStream out;
        private final PrintStream err;
        private Integer exitCode;

        Reporter(Path reportFile, PrintStream out, PrintStream err) {
            this.reportFile = reportFile;
            this.out = out;
            this.err = err;
        }

        synchronized boolean isDone() {
            return exitCode != null;
        }

        synchronized int exitCode() {
            return exitCode == null ? 1 : exitCode;
        }

        synchronized int pass() {
            if (exitCode != null) {
                return exitCode;
            }
            out.println(PASS);
            writeReport(PASS + System.lineSeparator());
            exitCode = 0;
            return 0;
        }

        synchronized int fail(String step, Throwable cause) {
            if (exitCode != null) {
                return exitCode;
            }
            StringWriter trace = new StringWriter();
            try (PrintWriter writer = new PrintWriter(trace)) {
                cause.printStackTrace(writer);
            }
            String text = FAIL + ": " + step + ": " + describe(cause) + System.lineSeparator() + trace;
            err.print(text);
            err.flush();
            writeReport(text);
            exitCode = 1;
            return 1;
        }

        /** The whole cause chain on one line, so the reason survives even a truncated log. */
        static String describe(Throwable cause) {
            StringBuilder text = new StringBuilder();
            for (Throwable t = cause; t != null; t = t.getCause()) {
                if (!text.isEmpty()) {
                    text.append(" | caused by ");
                }
                text.append(t.getClass().getName()).append(": ").append(t.getMessage());
                if (t.getCause() == t) {
                    break;
                }
            }
            return text.toString().replace('\r', ' ').replace('\n', ' ');
        }

        private void writeReport(String text) {
            if (reportFile == null) {
                return;
            }
            try {
                Path parent = reportFile.toAbsolutePath().getParent();
                if (parent != null) {
                    Files.createDirectories(parent);
                }
                Files.writeString(reportFile, text, StandardCharsets.UTF_8);
            } catch (IOException e) {
                err.println("Could not write the self-test report " + reportFile + ": " + e);
            }
        }
    }
}
