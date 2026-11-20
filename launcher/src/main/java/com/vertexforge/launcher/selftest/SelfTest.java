package com.vertexforge.launcher.selftest;

import com.google.gson.JsonArray;
import com.google.gson.JsonNull;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.vertexforge.launcher.Launcher;
import com.vertexforge.launcher.controller.MainController;
import com.vertexforge.launcher.engine.EngineContent;
import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineRegistryCodec;
import com.vertexforge.launcher.engine.EngineRegistryRepository;
import com.vertexforge.launcher.engine.LaunchPlan;
import com.vertexforge.launcher.engine.PickedEngine;
import com.vertexforge.launcher.env.DriveTypes;
import com.vertexforge.launcher.env.LauncherEnvironment;
import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.project.EditorLockProbe;
import com.vertexforge.launcher.project.EditorLockState;
import com.vertexforge.launcher.project.PickedProject;
import com.vertexforge.launcher.project.ProjectFile;
import com.vertexforge.launcher.project.ProjectFileCheck;
import com.vertexforge.launcher.project.ProjectFileReader;
import com.vertexforge.launcher.project.VersionCompatibility;
import com.vertexforge.launcher.repository.AddedProject;
import com.vertexforge.launcher.repository.RecentProjectsCodec;
import com.vertexforge.launcher.repository.RecentProjectsRepository;
import com.vertexforge.launcher.repository.RecentProjectsView;
import com.vertexforge.launcher.scriptlib.LegacyLibraryHashes;
import com.vertexforge.launcher.service.CreateOutcome;
import com.vertexforge.launcher.service.CreateRequest;
import com.vertexforge.launcher.service.JobMonitor;
import com.vertexforge.launcher.service.ProjectBackupService;
import com.vertexforge.launcher.service.ProjectHealthService;
import com.vertexforge.launcher.service.ProjectHub;
import com.vertexforge.launcher.service.ScriptLibraryStatusService;
import com.vertexforge.launcher.service.ScriptLibraryUpdater;
import com.vertexforge.launcher.settings.LauncherSettings;
import com.vertexforge.launcher.store.StoreStatus;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.template.LibraryManifest;
import com.vertexforge.launcher.template.TemplateCatalog;
import com.vertexforge.launcher.template.TemplateCatalogs;
import com.vertexforge.launcher.template.TemplateOptionKind;
import com.vertexforge.launcher.ui.MainView;
import com.vertexforge.launcher.util.LauncherLog;
import com.vertexforge.launcher.viewmodel.ProjectViewModel;
import javafx.application.Platform;
import javafx.collections.ObservableList;
import javafx.css.CssParser;
import javafx.css.Stylesheet;
import javafx.scene.Parent;
import javafx.scene.control.TableColumn;
import javafx.scene.control.TableView;

import java.io.IOException;
import java.io.PrintStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.net.URI;
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
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.stream.Stream;
import java.util.zip.ZipFile;

/**
 * {@code --self-test}: proves that a build, above all the packaged app image, has every class, module and resource
 * the launcher needs (VK-1677). It starts the JavaFX toolkit, loads the real main window and stylesheet, runs a
 * recent-projects store round trip, registers a fake engine and creates a project from that engine's templates and
 * script library (VK-1689), all inside a temp folder: it never shows a window, never starts a process and never
 * touches {@code ~/.vertexforge}.
 * <p>
 * Exit code 0 means pass and 1 means fail. {@code SELF-TEST PASS} goes to stdout and the failure reason to stderr;
 * with {@code --self-test-report=<file>} both also go to that file, which is the reliable channel for the packaged
 * exe (it has no console).
 */
public final class SelfTest {

    public static final String FLAG = "--self-test";
    public static final String REPORT_PREFIX = "--self-test-report=";
    /**
     * Also checks that the launcher knows its own exe ({@code jpackage.app-path}), from which it finds its bundled
     * engine (VK-1681), and that it runs as the module the app image is built as (VK-1692). Only the packaged app
     * image has either, so CI passes this flag only there.
     */
    public static final String PACKAGED_FLAG = "--self-test-packaged";
    /** The launcher's module (module-info.java). */
    static final String MODULE_NAME = "com.vertexforge.launcher";
    /** The modules the app image's jlink runtime links from the JavaFX jmods and the Gson jar (VK-1692). */
    private static final List<String> LINKED_MODULES = List.of(
            "javafx.base", "javafx.graphics", "javafx.controls", "javafx.fxml", "com.google.gson");
    static final Duration UI_TIMEOUT = Duration.ofSeconds(45);
    static final Duration WATCHDOG_TIMEOUT = Duration.ofSeconds(60);
    static final String PASS = "SELF-TEST PASS";
    static final String FAIL = "SELF-TEST FAIL";

    private static final List<String> REQUIRED_RESOURCES = List.of(
            MainView.FXML_RESOURCE,
            MainView.STYLESHEET_RESOURCE,
            "/images/icon.png",
            LegacyLibraryHashes.RESOURCE);
    /** The name of the project the self-test creates. */
    static final String PROJECT_NAME = "SelfTest";

    private SelfTest() {
        // Utility class - prevent instantiation
    }

    public static boolean isRequested(String[] args) {
        return Arrays.asList(args).contains(FLAG);
    }

    static boolean isPackagedRequested(String[] args) {
        return Arrays.asList(args).contains(PACKAGED_FLAG);
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
            if (isPackagedRequested(args)) {
                check("launcher location", () -> checkLauncherLocation(LauncherEnvironment.systemLauncherExecutable()));
                check("module layout", () -> checkModuleLayout(SelfTest.class.getModule(), ModuleLayer.boot(),
                        LauncherEnvironment.systemAppVersion(), LauncherEnvironment.systemJavaHome(),
                        LauncherEnvironment.systemLauncherExecutable()));
                check("code page", () -> checkCodePage(LauncherEnvironment.systemPathEncoding()));
            }
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
        // Read for projects without a .vflib.json (VK-1691), which the self-test's own project is not.
        check("legacy library hashes", LegacyLibraryHashes::bundled);
        check("store round trip", () -> checkStoreRoundTrip(workDir.resolve("store"), environment.clock(), log));
        // The engine first: the project is created from its templates and bound to it (VK-1689).
        EngineOption[] engine = new EngineOption[1];
        check("engine registry", () -> engine[0] = checkEngineRegistry(hub, workDir.resolve("engines"),
                environment.configDir().resolve(EngineRegistryRepository.FILE_NAME)));
        check("project creation", () -> checkProjectCreation(hub, workDir.resolve("projects"), engine[0]));
        check("existing project", () -> checkExistingProject(hub));
        check("project backup", () -> checkProjectBackup(hub));
        check("script library update", () -> checkScriptLibraryUpdate(hub, engine[0]));
        return hub;
    }

    /**
     * "Update Script Library…" end to end on the created project (VK-1691): a fresh project is up to date; after the
     * engine's library changes and the user edits a file the engine did not change, the update takes the engine's
     * change, adds its new file, keeps the user's edit, backs the old library up into the project's state folder and
     * records the engine's hashes; a second look then finds nothing to do.
     */
    private static void checkScriptLibraryUpdate(ProjectHub hub, EngineOption engine) throws Exception {
        Path projectFile = hub.listProjects().entries().getFirst().path();
        ProjectFile project = ProjectFileReader.read(projectFile);
        ScriptLibraryStatusService.EngineLibrary library = ScriptLibraryStatusService.EngineLibrary.of(engine)
                .orElseThrow(() -> new IllegalStateException("the engine names no script library"));
        ScriptLibraryUpdater.Request request = new ScriptLibraryUpdater.Request(projectFile,
                project.workingDirectory(), library.folder(), library.version());

        if (!(hub.previewScriptLibrary(request, JobMonitor.NONE) instanceof ScriptLibraryUpdater.PreviewOutcome.Ready
                fresh) || !fresh.preview().plan().upToDate()) {
            throw new IllegalStateException("a new project's library is not up to date with its engine's");
        }

        Path projectLibrary = projectFile.getParent().resolve("assets/scripts/lib");
        Files.writeString(library.folder().resolve("engine/Log.mt"), "public class Log {\n    // new\n}\n",
                StandardCharsets.UTF_8);
        Files.writeString(library.folder().resolve("engine/New.mt"), "public class New {\n}\n",
                StandardCharsets.UTF_8);
        String mine = "public class Behaviour {\n    // mine\n}\n";
        Files.writeString(projectLibrary.resolve("engine/oop/Behaviour.mt"), mine, StandardCharsets.UTF_8);

        if (!(hub.previewScriptLibrary(request, JobMonitor.NONE) instanceof ScriptLibraryUpdater.PreviewOutcome.Ready
                ready)) {
            throw new IllegalStateException("the preview did not succeed");
        }
        ScriptLibraryUpdater.UpdateOutcome outcome = hub.updateScriptLibrary(request, ready.preview().fingerprint(),
                false, JobMonitor.NONE);
        if (!(outcome instanceof ScriptLibraryUpdater.UpdateOutcome.Done done) || done.backup() == null) {
            throw new IllegalStateException("the update gave " + outcome);
        }
        if (!Files.readString(projectLibrary.resolve("engine/Log.mt"), StandardCharsets.UTF_8).contains("// new")
                || !Files.isRegularFile(projectLibrary.resolve("engine/New.mt"))
                || !Files.readString(projectLibrary.resolve("engine/oop/Behaviour.mt"), StandardCharsets.UTF_8)
                .equals(mine)) {
            throw new IllegalStateException("the library was not updated as planned");
        }
        LibraryManifest.Contents recorded = LibraryManifest.parse(
                Files.readAllBytes(projectLibrary.resolve(LibraryManifest.FILE_NAME)));
        if (!recorded.files().containsKey("engine/New.mt") || recorded.files().size() != 3) {
            throw new IllegalStateException("the new library manifest is wrong: " + recorded.files().keySet());
        }
        if (!done.backup().getParent().equals(ScriptLibraryUpdater.backupFolder(projectFile.getParent()))) {
            throw new IllegalStateException("the backup is not in the project's state folder: " + done.backup());
        }
        try (ZipFile zip = new ZipFile(done.backup().toFile())) {
            if (zip.getEntry("lib/engine/Log.mt") == null) {
                throw new IllegalStateException("the backup has no lib/engine/Log.mt");
            }
        }
        if (!(hub.previewScriptLibrary(request, JobMonitor.NONE) instanceof ScriptLibraryUpdater.PreviewOutcome.Ready
                again) || !again.preview().plan().upToDate()) {
            throw new IllegalStateException("a second update would still change the library");
        }
    }

    /**
     * "Back up and open" end to end on the created project (VK-1690): the zip is written beside the project folder,
     * never inside it, verified, and holds the project file; so the packaged image has what the backup needs
     * ({@code java.util.zip}, {@code FileStore}).
     */
    private static void checkProjectBackup(ProjectHub hub) throws Exception {
        Path projectFile = hub.listProjects().entries().getFirst().path();
        ProjectFile project = ProjectFileReader.read(projectFile);
        ProjectBackupService.BackupOutcome outcome = hub.backUpProject(new ProjectBackupService.BackupRequest(
                projectFile, project.workingDirectory(), VersionCompatibility.savedVersion(project).orElse(null)),
                JobMonitor.NONE);
        if (!(outcome instanceof ProjectBackupService.BackupOutcome.Done done)) {
            throw new IllegalStateException("the backup gave " + outcome);
        }
        Path folder = projectFile.getParent();
        if (!Files.isRegularFile(done.zip()) || !folder.getParent().equals(done.zip().getParent())) {
            throw new IllegalStateException("the backup is not a file beside the project folder: " + done.zip());
        }
        String entry = folder.getFileName() + "/" + projectFile.getFileName();
        try (ZipFile zip = new ZipFile(done.zip().toFile())) {
            if (zip.getEntry(entry) == null) {
                throw new IllegalStateException("the backup has no " + entry);
            }
        }
        Files.delete(done.zip());
    }

    /**
     * No user folder, no environment variables, no processes, no native calls: everything lives below
     * {@code workDir}.
     */
    static LauncherEnvironment hermeticEnvironment(Path workDir) {
        return new LauncherEnvironment(
                Clock.systemDefaultZone(),
                workDir.resolve("config"),
                Optional.empty(),
                name -> null,
                builder -> {
                    throw new IOException("the self-test never starts processes");
                },
                StandardCharsets.UTF_8,
                DriveTypes.unknown());
    }

    private static void checkResources() {
        for (String resource : REQUIRED_RESOURCES) {
            if (SelfTest.class.getResource(resource) == null) {
                throw new IllegalStateException("missing resource " + resource);
            }
        }
        // Projects are made from the engine's own templates (VK-1689): the launcher must carry none of its own.
        if (SelfTest.class.getResource("/templates/") != null) {
            throw new IllegalStateException("the launcher still carries a templates/ resource folder");
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

    /**
     * The project the creation check made reads back through the project file reader (VK-1685), and adding its
     * folder again finds the listed entry instead of adding a second one, so the list still holds exactly one.
     */
    private static void checkExistingProject(ProjectHub hub) throws Exception {
        List<RecentEntry> listed = hub.listProjects().entries();
        if (listed.size() != 1 || listed.getFirst().path() == null) {
            throw new IllegalStateException("the created project is not listed: " + listed);
        }
        Path projectFile = listed.getFirst().path();
        String name = ProjectFileReader.read(projectFile).projectName();
        if (!PROJECT_NAME.equals(name)) {
            throw new IllegalStateException("the project file reads back as \"" + name + "\"");
        }

        PickedProject picked = hub.inspectProject(projectFile.getParent()).get(UI_TIMEOUT.toSeconds(),
                TimeUnit.SECONDS);
        if (!(picked instanceof PickedProject.Found found) || !projectFile.equals(found.file())) {
            throw new IllegalStateException("the project folder was read as " + picked);
        }
        AddedProject added = hub.addExisting(found);
        if (!added.existed() || added.view().entries().size() != 1) {
            throw new IllegalStateException("adding the listed project again changed the list: " + added.view());
        }
    }

    /**
     * A project made from the fake engine's {@code empty} template with both options (VK-1689): its files are the
     * engine's, its placeholders are filled, its {@code .vfproj} carries the engine's version, and its list entry is
     * bound to the engine.
     */
    private static void checkProjectCreation(ProjectHub hub, Path projectsDir, EngineOption engine) throws Exception {
        Files.createDirectories(projectsDir);
        EngineContent content = EngineContent.of(engine).orElseThrow(() -> new IllegalStateException(
                "the fake engine offers nothing to create projects from: " + engine.health()));
        TemplateCatalogs.Answer answer = hub.listTemplates(content).get(UI_TIMEOUT.toSeconds(), TimeUnit.SECONDS);
        if (answer.catalog() == null) {
            throw new IllegalStateException("the fake engine's templates could not be read: " + answer.problem());
        }
        TemplateCatalog catalog = answer.catalog();
        if (catalog.find(TemplateCatalog.DEFAULT_TEMPLATE).isEmpty()
                || catalog.unavailableReason(TemplateOptionKind.EXAMPLES).isPresent()
                || catalog.unavailableReason(TemplateOptionKind.VFX_LIBRARY).isPresent()) {
            throw new IllegalStateException("the fake engine's catalog is incomplete: " + catalog.notes()
                    + " " + catalog.unavailableOptions());
        }

        CreateOutcome outcome = hub.createProject(new CreateRequest(PROJECT_NAME, projectsDir, content,
                TemplateCatalog.DEFAULT_TEMPLATE, Set.of(TemplateOptionKind.EXAMPLES.id(),
                TemplateOptionKind.VFX_LIBRARY.id())));
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
        if (!content.engineId().map(UUID::toString).equals(Optional.ofNullable(listed.getFirst().engineId()))) {
            throw new IllegalStateException("the new project is not bound to the engine it was created from: "
                    + listed.getFirst().engineId());
        }
        checkCreatedFiles(outcome.projectPath(), content);

        // The health check reads the file and the folder's editor lock (VK-1686): what the launcher creates is a
        // project the editor accepts, no editor has it open, and the check created nothing in it.
        ProjectFileCheck check = hub.checkProject(listed.getFirst(), true).get(UI_TIMEOUT.toSeconds(),
                TimeUnit.SECONDS);
        if (!(check instanceof ProjectFileCheck.Readable readable) || readable.file().structureProblem() != null
                || !(readable.lock() instanceof EditorLockState.Free)) {
            throw new IllegalStateException("the new project's health check gave " + check);
        }
        ProjectStatus status = ProjectHealthService.evaluate(check, null, null,
                ProjectHealthService.FolderEditor.NONE);
        if (!status.isOk()) {
            throw new IllegalStateException("the new project's status is " + status);
        }
        if (Files.exists(EditorLockProbe.lockFile(outcome.projectPath().getParent()).getParent())) {
            throw new IllegalStateException("the health check created the editor's state folder");
        }
    }

    /** The fake engine's files arrived as the template lays them out, with the placeholders filled in. */
    private static void checkCreatedFiles(Path projectFile, EngineContent content) throws IOException {
        Path project = projectFile.getParent();
        JsonObject vfproj = JsonParser.parseString(Files.readString(projectFile, StandardCharsets.UTF_8))
                .getAsJsonObject();
        if (!"assets".equals(vfproj.get("workingDirectory").getAsString())
                || !content.displayVersion().equals(vfproj.get("engineVersion").getAsString())) {
            throw new IllegalStateException("the .vfproj does not follow the template and the engine: " + vfproj);
        }
        String main = Files.readString(project.resolve("assets/scripts/game/Main.mt"), StandardCharsets.UTF_8);
        String mtproj = Files.readString(project.resolve("assets/scripts/scripts.mtproj"), StandardCharsets.UTF_8);
        if (!main.contains("Hello from " + PROJECT_NAME) || !mtproj.contains("Name=\"" + PROJECT_NAME + "\"")) {
            throw new IllegalStateException("the template's placeholders were not filled in");
        }
        for (Map.Entry<String, String> file : FAKE_LIBRARY.entrySet()) {
            byte[] copied = Files.readAllBytes(project.resolve("assets/scripts/lib").resolve(file.getKey()));
            if (!Arrays.equals(file.getValue().getBytes(StandardCharsets.UTF_8), copied)) {
                throw new IllegalStateException("the script library file " + file.getKey() + " changed in the copy");
            }
        }
        JsonObject libraryManifest = JsonParser.parseString(Files.readString(
                project.resolve("assets/scripts/lib").resolve(LibraryManifest.FILE_NAME), StandardCharsets.UTF_8))
                .getAsJsonObject();
        if (libraryManifest.get("formatVersion").getAsInt() != LibraryManifest.FORMAT_VERSION
                || libraryManifest.getAsJsonObject("files").size() != FAKE_LIBRARY.size()) {
            throw new IllegalStateException("the library manifest is wrong: " + libraryManifest);
        }
        if (!Files.isRegularFile(project.resolve("assets/scripts/game/examples/Example.mt"))
                || !Files.isRegularFile(project.resolve("assets/vfx/library/spark.vfVFX"))) {
            throw new IllegalStateException("the options' files were not copied");
        }
    }

    /** The fake engine's script library: two files, one with CRLF line endings. */
    private static final Map<String, String> FAKE_LIBRARY = fakeLibrary();

    private static Map<String, String> fakeLibrary() {
        Map<String, String> files = new LinkedHashMap<>();
        files.put("engine/Log.mt", "public class Log {\r\n}\r\n");
        files.put("engine/oop/Behaviour.mt", "public class Behaviour {\n}\n");
        return files;
    }

    /**
     * The content of a fake engine laid out like this repository's {@code assets/}: an {@code empty} template, a
     * two-file script library, one example and one effect.
     */
    private static void writeFakeContent(Path root) throws IOException {
        Path template = Files.createDirectories(root.resolve("assets/templates/empty"));
        Files.writeString(template.resolve("template.json"), """
                {"formatVersion": 1, "id": "empty", "name": "Empty", "minimumEngineVersion": "1.0.0",
                 "workingDirectory": "assets", "startupScene": "scenes/Main.vfScene",
                 "scriptLibrary": "assets/scripts/lib",
                 "options": [
                   {"id": "examples", "name": "Include examples", "target": "assets/scripts/game/examples"},
                   {"id": "vfxLibrary", "name": "Include VFX library", "target": "assets/vfx/library"}]}
                """, StandardCharsets.UTF_8);
        Path scripts = Files.createDirectories(template.resolve("project/assets/scripts/game")).getParent();
        Files.writeString(scripts.resolve("scripts.mtproj"),
                "<Project Name=\"{{PROJECT_NAME}}\" Version=\"1.0.0\"></Project>\n", StandardCharsets.UTF_8);
        Files.writeString(scripts.resolve("game/Main.mt"), "string hello = \"Hello from {{PROJECT_NAME}}\";\n",
                StandardCharsets.UTF_8);
        Files.writeString(template.resolve("project/.gitignore"), "/.vertexforge/\n", StandardCharsets.UTF_8);
        for (Map.Entry<String, String> file : FAKE_LIBRARY.entrySet()) {
            Path target = root.resolve("assets/scripts/lib").resolve(file.getKey());
            Files.createDirectories(target.getParent());
            Files.writeString(target, file.getValue(), StandardCharsets.UTF_8);
        }
        Files.writeString(Files.createDirectories(root.resolve("assets/scripts/game/examples")).resolve("Example.mt"),
                "public class Example {\n}\n", StandardCharsets.UTF_8);
        Files.writeString(Files.createDirectories(root.resolve("assets/vfx/library")).resolve("spark.vfVFX"), "{}\n",
                StandardCharsets.UTF_8);
    }

    /**
     * The engine registry end to end (VK-1681): a fake Development build laid out like this repository's
     * ({@code bin/Editor/Development/x64} with an {@code engine.json} whose working directory is
     * {@code ../../../../VFEngine/editor}, and the content folders of VK-1689) is added through its manifest, checks
     * OK, starts in that working directory, opens an unbound project and binds it; {@code engines.json} is version 1
     * with no temp file left behind. Returns the engine as checked.
     */
    static EngineOption checkEngineRegistry(ProjectHub hub, Path root, Path enginesFile) throws Exception {
        Path exeDir = Files.createDirectories(root.resolve("bin/Editor/Development/x64"));
        Path workingDir = Files.createDirectories(root.resolve("VFEngine/editor"));
        Path editor = Files.writeString(exeDir.resolve("Editor.exe"), "not a real editor");
        editor.toFile().setExecutable(true);
        writeFakeContent(root);
        JsonObject manifest = new JsonObject();
        manifest.addProperty("manifestVersion", 1);
        manifest.addProperty("engineVersion", "1.0.0");
        manifest.addProperty("configuration", "Development");
        manifest.add("gitCommit", JsonNull.INSTANCE);
        manifest.addProperty("editorExe", "Editor.exe");
        manifest.addProperty("workingDirectory", "../../../../VFEngine/editor");
        manifest.addProperty("templatesDir", "../../../../assets/templates");
        manifest.addProperty("scriptLibDir", "../../../../assets/scripts/lib");
        manifest.addProperty("examplesDir", "../../../../assets/scripts/game/examples");
        manifest.addProperty("vfxLibraryDir", "../../../../assets/vfx/library");
        Files.writeString(exeDir.resolve(EngineManifest.FILE_NAME), manifest.toString(), StandardCharsets.UTF_8);

        PickedEngine picked = hub.inspectEngine(exeDir.resolve(EngineManifest.FILE_NAME))
                .get(UI_TIMEOUT.toSeconds(), TimeUnit.SECONDS);
        if (!(picked instanceof PickedEngine.Usable usable)) {
            throw new IllegalStateException("the fake Development build was refused: " + picked);
        }
        UUID id = hub.addEngine(usable).engine().id();

        EngineOptions options = hub.checkEngines(hub.listEngines()).get(UI_TIMEOUT.toSeconds(), TimeUnit.SECONDS);
        EngineOption engine = options.find(id)
                .orElseThrow(() -> new IllegalStateException("the added engine is not listed: " + options));
        if (!engine.isOk() || !workingDir.toRealPath().equals(engine.health().workingDirectory())) {
            throw new IllegalStateException("the engine did not check OK in its manifest's working directory: "
                    + engine.health());
        }
        if (!"1.0.0 Development".equals(engine.versionText())) {
            throw new IllegalStateException("the engine's version reads as " + engine.versionText());
        }
        if (!(hub.planLaunch(options, null) instanceof LaunchPlan.Ready ready)
                || !ready.engine().equals(engine) || !ready.bindTo().equals(Optional.of(id))) {
            throw new IllegalStateException("an unbound project would not open with, and bind to, the only engine");
        }

        JsonObject written = JsonParser.parseString(Files.readString(enginesFile, StandardCharsets.UTF_8))
                .getAsJsonObject();
        if (written.get("version").getAsInt() != EngineRegistryCodec.CURRENT_VERSION
                || written.getAsJsonArray(EngineRegistryCodec.ENGINES).size() != 1) {
            throw new IllegalStateException("engines.json is not what was added: " + written);
        }
        try (Stream<Path> files = Files.list(enginesFile.getParent())) {
            List<String> left = files.map(path -> path.getFileName().toString())
                    .filter(name -> name.endsWith(".tmp"))
                    .toList();
            if (!left.isEmpty()) {
                throw new IllegalStateException("temporary files were left behind: " + left);
            }
        }
        return engine;
    }

    /**
     * The packaged launcher knows its own exe: {@code jpackage.app-path} names a file whose folder holds the app
     * image's {@code app} and {@code runtime} folders. The bundled engine is found from there (VK-1681).
     */
    static void checkLauncherLocation(Optional<Path> launcherExecutable) {
        Path exe = launcherExecutable.orElseThrow(() -> new IllegalStateException(
                LauncherEnvironment.APP_PATH_PROPERTY + " is not set: this is not the packaged launcher"));
        if (!Files.isRegularFile(exe)) {
            throw new IllegalStateException(LauncherEnvironment.APP_PATH_PROPERTY + " names " + exe
                    + ", which is not a file");
        }
        Path imageRoot = exe.getParent();
        if (imageRoot == null || !Files.isDirectory(imageRoot.resolve("app"))
                || !Files.isDirectory(imageRoot.resolve("runtime"))) {
            throw new IllegalStateException("the launcher exe " + exe + " is not at the root of its app image");
        }
    }

    /**
     * The packaged launcher runs in the UTF-8 code page (VK-1693): its exe carries the activeCodePage manifest the
     * image profile merges in, so jpackage's native launcher hands the JVM its runtime folder, its options and
     * {@code main}'s arguments without loss. Without it the launcher cannot start from a folder the system's ANSI code
     * page cannot spell, and a {@code .vfproj} double-clicked in such a folder arrives as a path full of {@code ?}.
     */
    static void checkCodePage(String pathEncoding) {
        if (!"UTF-8".equals(pathEncoding)) {
            throw new IllegalStateException("the launcher runs in the code page " + pathEncoding + ", not UTF-8: its"
                    + " exe lacks the activeCodePage manifest (launcher/pom.xml, src/jpackage/utf8-code-page.manifest)");
        }
    }

    /**
     * The packaged launcher runs as the module the app image is built as (VK-1692): named, carrying the image's
     * version, exported and open exactly where JavaFX's reflection needs it, with native access, from the image's
     * {@code app/mods}, on the image's own runtime, whose JavaFX and Gson are linked modules and whose JavaFX natives
     * sit in {@code runtime/bin/javafx} (so nothing is unpacked into {@code ~/.openjfx}). {@code Application.launch}
     * never runs in the self-test, so the export it needs is checked on the module instead.
     */
    static void checkModuleLayout(Module launcher, ModuleLayer layer, Optional<String> appVersion, Path javaHome,
                                  Optional<Path> launcherExecutable) throws IOException {
        if (!launcher.isNamed()) {
            throw new IllegalStateException("the launcher runs from the classpath, not as the module " + MODULE_NAME);
        }
        if (!launcher.getName().equals(MODULE_NAME)) {
            throw new IllegalStateException("the launcher runs as the module " + launcher.getName() + ", not "
                    + MODULE_NAME);
        }
        String version = appVersion.orElseThrow(() -> new IllegalStateException(
                LauncherEnvironment.APP_VERSION_PROPERTY + " is not set"));
        Optional<String> moduleVersion = launcher.getDescriptor().rawVersion();
        if (!moduleVersion.equals(Optional.of(version))) {
            throw new IllegalStateException("the module version " + moduleVersion.orElse("(none)")
                    + " is not the app image's version " + version);
        }

        Module graphics = bootModule(layer, "javafx.graphics");
        Module fxml = bootModule(layer, "javafx.fxml");
        String rootPackage = Launcher.class.getPackageName();
        if (!launcher.isExported(rootPackage, graphics)) {
            throw new IllegalStateException(rootPackage + " is not exported to javafx.graphics, so Application.launch"
                    + " cannot construct the launcher's Application");
        }
        String controllerPackage = MainController.class.getPackageName();
        if (!launcher.isOpen(controllerPackage, fxml)) {
            throw new IllegalStateException(controllerPackage + " is not open to javafx.fxml, so FXMLLoader cannot"
                    + " inject the main window's controller");
        }
        for (Module module : List.of(launcher, graphics)) {
            if (!module.isNativeAccessEnabled()) {
                throw new IllegalStateException("the module " + module.getName()
                        + " has no native access (--enable-native-access)");
            }
        }

        for (String name : LINKED_MODULES) {
            URI location = moduleLocation(layer, name);
            if (!"jrt".equals(location.getScheme())) {
                throw new IllegalStateException("the module " + name + " comes from " + location
                        + ", not from the app image's runtime");
            }
        }
        Path imageRoot = launcherExecutable.map(Path::getParent).orElseThrow(() -> new IllegalStateException(
                LauncherEnvironment.APP_PATH_PROPERTY + " is not set: this is not the packaged launcher"));
        URI ownLocation = moduleLocation(layer, MODULE_NAME);
        Path mods = imageRoot.resolve("app").resolve("mods");
        if (!"file".equals(ownLocation.getScheme()) || Path.of(ownLocation).getParent() == null
                || !Files.isSameFile(Path.of(ownLocation).getParent(), mods)) {
            throw new IllegalStateException("the launcher module comes from " + ownLocation + ", not from " + mods);
        }
        if (!Files.isSameFile(javaHome, imageRoot.resolve("runtime"))) {
            throw new IllegalStateException("the launcher runs on " + javaHome + ", not on the app image's runtime");
        }
        // JavaFX loads its natives from <java.home>/bin/javafx when it runs from a jlink runtime (NativeLibLoader),
        // where the jmods put them; only when that fails does it unpack them from its jar into ~/.openjfx.
        Path glass = javaHome.resolve("bin").resolve("javafx").resolve("glass.dll");
        if (!Files.isRegularFile(glass)) {
            throw new IllegalStateException("the runtime has no " + glass + ", so JavaFX would unpack its natives"
                    + " into ~/.openjfx");
        }
    }

    private static Module bootModule(ModuleLayer layer, String name) {
        return layer.findModule(name).orElseThrow(() -> new IllegalStateException(
                "the module " + name + " is not in the boot layer"));
    }

    private static URI moduleLocation(ModuleLayer layer, String name) {
        return layer.configuration().findModule(name)
                .flatMap(resolved -> resolved.reference().location())
                .orElseThrow(() -> new IllegalStateException("the module " + name + " is not in the boot layer"));
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
            loaded.shutdownNow();
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
            loaded[0].shutdownNow();
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
