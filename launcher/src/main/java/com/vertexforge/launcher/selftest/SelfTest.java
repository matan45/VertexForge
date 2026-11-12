package com.vertexforge.launcher.selftest;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.vertexforge.launcher.controller.MainController;
import com.vertexforge.launcher.env.LauncherEnvironment;
import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.repository.RecentProjectsCodec;
import com.vertexforge.launcher.repository.RecentProjectsRepository;
import com.vertexforge.launcher.repository.RecentProjectsView;
import com.vertexforge.launcher.service.CreateOutcome;
import com.vertexforge.launcher.service.ProjectCreator;
import com.vertexforge.launcher.service.ProjectHub;
import com.vertexforge.launcher.settings.LauncherSettings;
import com.vertexforge.launcher.store.StoreStatus;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.ui.MainView;
import com.vertexforge.launcher.util.LauncherLog;
import com.vertexforge.launcher.util.ZipExtractor;
import com.vertexforge.launcher.viewmodel.ProjectViewModel;
import javafx.application.Platform;
import javafx.collections.ObservableList;
import javafx.css.CssParser;
import javafx.css.Stylesheet;
import javafx.scene.Parent;
import javafx.scene.control.TableColumn;
import javafx.scene.control.TableView;

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
import java.time.Instant;
import java.time.ZoneId;
import java.util.Arrays;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
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
 * the launcher needs (VK-1677). It starts the JavaFX toolkit, loads the real main window and stylesheet, runs a
 * recent-projects store and a zip round trip and creates a project from the bundled templates, all inside a temp
 * folder: it never shows a window, never starts a process and never touches {@code ~/.vertexforge}.
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
        check("store round trip", () -> checkStoreRoundTrip(workDir.resolve("store"), environment.clock(), log));
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

    /**
     * The recent-projects store end to end (VK-1678): a version 1 file with members this build does not know is
     * read, a project is added (lock file, temp file, {@code force}, {@code ATOMIC_MOVE}), and the result must be
     * version 2 with every unknown member intact, stable ids and no temp file left behind.
     */
    static void checkStoreRoundTrip(Path dir, Clock clock, LauncherLog log) throws IOException, StoreWriteException {
        Files.createDirectories(dir);
        Path file = dir.resolve(RecentProjectsRepository.FILE_NAME);
        Path oldProject = dir.resolve("Old").resolve("Old.vfproj").toAbsolutePath();

        JsonObject future = new JsonObject();
        future.addProperty("x", 1);
        JsonObject entry = new JsonObject();
        entry.addProperty("name", "Old");
        entry.addProperty("path", oldProject.toString());
        entry.addProperty("lastOpened", "2026-01-02T03:04:05Z");
        entry.add("future", future);
        JsonArray projects = new JsonArray();
        projects.add(entry);
        JsonObject v1 = new JsonObject();
        v1.addProperty("version", 1);
        v1.addProperty("launcherNote", "kept");
        v1.add("projects", projects);
        Files.writeString(file, v1.toString(), StandardCharsets.UTF_8);

        RecentProjectsRepository repository = new RecentProjectsRepository(
                RecentProjectsRepository.openStore(file, clock, log), UUID::randomUUID, clock, log);
        RecentProjectsView loaded = repository.load();
        if (loaded.status() != StoreStatus.LEGACY || loaded.entries().size() != 1) {
            throw new IllegalStateException("the version 1 file did not load: " + loaded);
        }
        UUID oldId = loaded.entries().getFirst().id();

        repository.add(dir.resolve("New").resolve("New.vfproj").toAbsolutePath(), "New",
                LauncherSettings.DEFAULT_RECENT_PROJECTS_LIMIT);

        JsonObject written = JsonParser.parseString(Files.readString(file, StandardCharsets.UTF_8)).getAsJsonObject();
        if (written.get("version").getAsInt() != RecentProjectsCodec.CURRENT_VERSION
                || !"kept".equals(written.get("launcherNote").getAsString())) {
            throw new IllegalStateException("the file was not migrated with its unknown members: " + written);
        }
        JsonArray writtenProjects = written.getAsJsonArray("projects");
        if (writtenProjects.size() != 2) {
            throw new IllegalStateException("the store did not keep both projects: " + written);
        }
        JsonObject old = writtenProjects.get(0).getAsJsonObject();
        if (!old.has("future") || !future.equals(old.get("future"))
                || !oldId.toString().equals(old.get(RecentEntry.ID).getAsString())) {
            throw new IllegalStateException("the old entry lost its id or an unknown member: " + old);
        }
        UUID.fromString(writtenProjects.get(1).getAsJsonObject().get(RecentEntry.ID).getAsString());

        try (Stream<Path> files = Files.list(dir)) {
            List<String> left = files.map(path -> path.getFileName().toString())
                    .filter(name -> name.endsWith(".tmp"))
                    .toList();
            if (!left.isEmpty()) {
                throw new IllegalStateException("temporary files were left behind: " + left);
            }
        }
        if (!Files.isRegularFile(dir.resolve(RecentProjectsRepository.FILE_NAME + ".lock"))) {
            throw new IllegalStateException("the store never created its lock file");
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

    private static void checkProjectCreation(ProjectHub hub, Path projectsDir) throws Exception {
        Files.createDirectories(projectsDir);
        CreateOutcome outcome = hub.createProject("SelfTest", projectsDir);
        if (!outcome.created()) {
            throw new IllegalStateException("project creation failed: " + outcome.result().errorMessage());
        }
        if (!outcome.listed()) {
            throw new IllegalStateException("the new project was not added to the recent list: "
                    + outcome.listError().getMessage(), outcome.listError());
        }

        List<RecentEntry> listed = hub.listProjects().entries();
        if (listed.size() != 1 || !outcome.projectPath().equals(listed.getFirst().path())) {
            throw new IllegalStateException("the recent list did not round-trip the new project: " + listed);
        }

        ProjectStatus status = hub.checkStatus(listed.getFirst()).get(UI_TIMEOUT.toSeconds(), TimeUnit.SECONDS);
        if (!status.isOk()) {
            throw new IllegalStateException("the new project's status is " + status);
        }

        try (Stream<Path> lib = Files.walk(outcome.projectPath().resolveSibling("scripts").resolve("lib"))) {
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

    /**
     * Loads the stylesheet and the real main window on the FX thread, waits (on this thread, never the FX thread) for
     * the window's first background list read, then checks the table it filled.
     */
    private static void runUiChecks(ProjectHub hub) throws CheckFailure {
        MainView.Loaded loaded = onFxThread("main window", () -> uiChecks(hub));
        try {
            check("project list", () -> loaded.controller().firstLoad().get(UI_TIMEOUT.toSeconds(), TimeUnit.SECONDS));
            onFxThread("project table", () -> {
                checkProjectTable(loaded.controller());
                return null;
            });
        } finally {
            loaded.background().shutdownNow();
        }
    }

    @FunctionalInterface
    interface FxCheck<T> {
        T run() throws Exception;
    }

    /** Runs {@code body} on the FX thread and waits for it here; a failure keeps the step its check named. */
    private static <T> T onFxThread(String step, FxCheck<T> body) throws CheckFailure {
        CompletableFuture<T> done = new CompletableFuture<>();
        Platform.runLater(() -> {
            try {
                done.complete(body.run());
            } catch (Throwable t) {
                done.completeExceptionally(t);
            }
        });

        try {
            return done.get(UI_TIMEOUT.toSeconds(), TimeUnit.SECONDS);
        } catch (ExecutionException e) {
            if (e.getCause() instanceof CheckFailure failure) {
                throw failure;
            }
            throw new CheckFailure(step, e.getCause());
        } catch (TimeoutException e) {
            throw new CheckFailure(step,
                    new TimeoutException("the UI checks did not finish within " + UI_TIMEOUT.toSeconds() + " s"));
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new CheckFailure(step, e);
        }
    }

    private static MainView.Loaded uiChecks(ProjectHub hub) throws CheckFailure {
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

        MainView.Loaded[] loaded = new MainView.Loaded[1];
        check("main.fxml", () -> loaded[0] = MainView.load(hub));

        Parent root = loaded[0].root();
        try {
            check("main window scene", () -> {
                MainView.createScene(root);
                root.applyCss();
                requireNoCssErrors(cssErrors);
            });
        } catch (CheckFailure failure) {
            loaded[0].background().shutdownNow();
            throw failure;
        }
        return loaded[0];
    }

    /**
     * The table shows the project the core checks created, and every column has a value for it. This is what catches
     * a column bound to a property its row does not have (the blank Last Opened column of VK-1678).
     */
    private static void checkProjectTable(MainController controller) throws CheckFailure {
        check("project table", () -> {
            TableView<ProjectViewModel> table = controller.projectsTable();
            if (table.getItems().size() != 1) {
                throw new IllegalStateException("the table lists " + table.getItems().size()
                        + " projects instead of the 1 the self-test created");
            }
            for (TableColumn<ProjectViewModel, ?> column : table.getColumns()) {
                Object value = column.getCellData(0);
                if (value == null || value.toString().isBlank()) {
                    throw new IllegalStateException("the \"" + column.getText() + "\" column is empty");
                }
            }
            String formatted = ProjectViewModel.formatLastOpened(Instant.now(), ZoneId.systemDefault());
            if (formatted.isBlank() || ProjectViewModel.UNKNOWN_DATE.equals(formatted)) {
                throw new IllegalStateException("the Last Opened format produced \"" + formatted + "\"");
            }
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
