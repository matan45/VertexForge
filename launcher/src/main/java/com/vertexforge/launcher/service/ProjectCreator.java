package com.vertexforge.launcher.service;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.vertexforge.launcher.model.ProjectCreationResult;
import com.vertexforge.launcher.util.LauncherLog;
import com.vertexforge.launcher.util.ProjectValidator;
import com.vertexforge.launcher.util.TransientIo;
import com.vertexforge.launcher.util.ZipExtractor;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.AccessDeniedException;
import java.nio.file.DirectoryNotEmptyException;
import java.nio.file.DirectoryStream;
import java.nio.file.FileAlreadyExistsException;
import java.nio.file.FileSystemException;
import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.LinkOption;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.attribute.BasicFileAttributes;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.function.Supplier;
import java.util.stream.Stream;

/**
 * Creates a project so that it either fully exists or nothing is left behind (VK-1679).
 * <p>
 * Everything is checked before anything is written. The project is then built in a sibling staging folder
 * ({@code .<name>.creating-<uuid>}) and renamed into place in one step that never replaces anything. A failure or a
 * cancel before that rename deletes the staging folder and nothing else: the creator never deletes a folder it did not
 * create. The one exception is an EMPTY folder that already had the project's name; it is removed just before the
 * rename and put back if the rename fails.
 */
public class ProjectCreator {

    // Packed from assets/scripts/lib at build time (pom.xml, pack-script-lib).
    public static final String LIBRARY_ZIP_RESOURCE = "/templates/script-library.zip";
    public static final String SCENE_TEMPLATE_RESOURCE = "/templates/Main.vfScene";
    /** Between the dot plus the project name and the UUID in a staging folder's name. */
    public static final String STAGING_INFIX = ".creating-";

    public static final String LOCATION_NOT_ABSOLUTE = "The location must be a full path, such as C:\\Projects.";

    /** The final rename is retried for about 5 s: an antivirus scan of the new files can hold the folder that long. */
    static final int COMMIT_ATTEMPTS = 8;
    static final Duration COMMIT_FIRST_DELAY = Duration.ofMillis(100);
    static final Duration COMMIT_MAX_DELAY = Duration.ofSeconds(1);
    private static final int CLEANUP_ATTEMPTS = 5;
    private static final Duration CLEANUP_FIRST_DELAY = Duration.ofMillis(50);
    private static final Duration CLEANUP_MAX_DELAY = Duration.ofMillis(500);

    private static final String ENGINE_VERSION = "VertexForge 1.0.0";

    /** What is at the project folder's path before anything is written. */
    enum TargetState {
        ABSENT,
        EMPTY_FOLDER
    }

    /** {@code state} when the project may go there, else {@code problem} says why not. */
    record TargetInspection(TargetState state, String problem) {
        static TargetInspection ok(TargetState state) {
            return new TargetInspection(state, null);
        }

        static TargetInspection refused(String problem) {
            return new TargetInspection(null, problem);
        }

        boolean refused() {
            return problem != null;
        }
    }

    /** Thrown from inside the writes when the user cancels; an IOException so that it passes through ZipExtractor. */
    private static final class CreationCancelled extends IOException {
        CreationCancelled() {
            super("cancelled");
        }
    }

    private final Gson gson;
    private final Clock clock;
    private final LauncherLog log;
    private final Supplier<UUID> ids;
    private final DirectoryMover mover;
    private final Duration commitFirstDelay;
    private final Duration commitMaxDelay;
    private final ProjectTemplate.Source templates;

    public ProjectCreator(Clock clock, LauncherLog log) {
        this(clock, log, UUID::randomUUID, DirectoryMover.SYSTEM, COMMIT_FIRST_DELAY, COMMIT_MAX_DELAY,
                ProjectTemplate.BUNDLED);
    }

    ProjectCreator(Clock clock, LauncherLog log, Supplier<UUID> ids, DirectoryMover mover, Duration commitFirstDelay,
                   Duration commitMaxDelay, ProjectTemplate.Source templates) {
        this.clock = Objects.requireNonNull(clock, "clock");
        this.log = Objects.requireNonNull(log, "log");
        this.ids = Objects.requireNonNull(ids, "ids");
        this.mover = Objects.requireNonNull(mover, "mover");
        this.commitFirstDelay = Objects.requireNonNull(commitFirstDelay, "commitFirstDelay");
        this.commitMaxDelay = Objects.requireNonNull(commitMaxDelay, "commitMaxDelay");
        this.templates = Objects.requireNonNull(templates, "templates");
        // disableHtmlEscaping: keeps "Tom & Jerry's Game" readable in the .vfproj instead of \u0026 and \u0027.
        this.gson = new GsonBuilder()
                .setPrettyPrinting()
                .disableHtmlEscaping()
                .create();
    }

    /**
     * Creates a new project with the specified name at the given location, watched by nobody.
     * <p>
     * Creates the following structure:
     * <pre>
     * {projectName}/
     * ├── {projectName}.vfproj
     * ├── scenes/
     * │   └── Main.vfScene
     * ├── assets/
     * └── scripts/
     *     ├── scripts.mtproj
     *     ├── game/
     *     │   └── Main.mt
     *     ├── compiled/
     *     └── lib/
     * </pre>
     *
     * @param projectName     the name of the project, exactly as typed
     * @param parentDirectory the absolute folder the project folder goes into
     * @return the result of the creation operation
     */
    public ProjectCreationResult createProject(String projectName, Path parentDirectory) {
        return create(new CreateRequest(projectName, parentDirectory), CreationMonitor.NONE);
    }

    /** Creates the project, reporting every file to {@code monitor} and stopping when it asks to. Never throws. */
    public ProjectCreationResult create(CreateRequest request, CreationMonitor monitor) {
        Objects.requireNonNull(request, "request");
        Objects.requireNonNull(monitor, "monitor");
        String name = request.name();

        // 1-5: every check, before anything is written.
        Optional<ProjectNameRules.Problem> nameProblem = ProjectNameRules.checkName(name);
        if (nameProblem.isPresent()) {
            return ProjectCreationResult.failure(nameProblem.get().message());
        }
        if (!request.location().isAbsolute()) {
            return ProjectCreationResult.failure(LOCATION_NOT_ABSOLUTE);
        }
        Path location = request.location().normalize();
        Path target;
        try {
            target = location.resolve(name);
        } catch (InvalidPathException e) {
            return ProjectCreationResult.failure("The project name can't be used as a folder name: " + e.getReason());
        }

        ProjectTemplate template;
        try {
            template = templates.load();
        } catch (IOException e) {
            return ProjectCreationResult.failure(describe(e));
        }
        Optional<ProjectNameRules.Problem> lengthProblem = ProjectNameRules.checkPathLength(location, name, template);
        if (lengthProblem.isPresent()) {
            return ProjectCreationResult.failure(lengthProblem.get().message());
        }

        Optional<String> locationProblem = inspectLocation(location);
        if (locationProblem.isPresent()) {
            return ProjectCreationResult.failure(locationProblem.get());
        }
        TargetInspection inspection = inspectTarget(location, name);
        if (inspection.refused()) {
            return ProjectCreationResult.failure(inspection.problem());
        }

        // 6
        if (monitor.cancelRequested()) {
            return ProjectCreationResult.cancelled(null);
        }

        // 7: from here on this folder is ours, and it is the only thing a failure may delete.
        Path staging = location.resolve("." + name + STAGING_INFIX + ids.get());
        try {
            Files.createDirectory(staging);
        } catch (IOException e) {
            return ProjectCreationResult.failure("Could not create a folder in " + location + ": " + describe(e));
        }

        // 8-9
        try {
            writeProject(name, staging, template, new Progress(monitor, template.fileCount()));
            if (monitor.cancelRequested()) {
                throw new CreationCancelled();
            }
        } catch (CreationCancelled e) {
            return abandon(location, name, staging, target, false, ProjectCreationResult.cancelled(null));
        } catch (IOException | RuntimeException e) {
            return abandon(location, name, staging, target, false,
                    ProjectCreationResult.failure("Could not create the project: " + describe(e)));
        }

        // 10: the commit. A cancel is no longer possible.
        return commit(location, name, staging, target);
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Checks (also used by LocationChecker, so the dialog and the creator agree)
    // -----------------------------------------------------------------------------------------------------------------

    /** Why projects can't be created in {@code location}, an absolute folder; empty when they can. Blocks on I/O. */
    static Optional<String> inspectLocation(Path location) {
        BasicFileAttributes attributes;
        try {
            attributes = Files.readAttributes(location, BasicFileAttributes.class);
        } catch (NoSuchFileException e) {
            return Optional.of("The folder " + location + " doesn't exist.");
        } catch (IOException e) {
            return Optional.of("Could not read the folder " + location + ": " + describe(e));
        }
        if (!attributes.isDirectory()) {
            return Optional.of(location + " is not a folder.");
        }
        if (!Files.isWritable(location)) {
            return Optional.of("You don't have permission to create folders in " + location + ".");
        }
        return Optional.empty();
    }

    /**
     * Whether a project called {@code name} may go into {@code location}: nothing has that name yet, or it is an EMPTY
     * folder that really lives there (not a link or a junction to a folder elsewhere). Blocks on I/O.
     */
    static TargetInspection inspectTarget(Path location, String name) {
        Path target = location.resolve(name);
        BasicFileAttributes attributes;
        try {
            attributes = Files.readAttributes(target, BasicFileAttributes.class, LinkOption.NOFOLLOW_LINKS);
        } catch (NoSuchFileException e) {
            return TargetInspection.ok(TargetState.ABSENT);
        } catch (IOException e) {
            return TargetInspection.refused("Could not check " + target + ": " + describe(e));
        }

        if (attributes.isSymbolicLink()) {
            return linkRefusal(location, name);
        }
        if (!attributes.isDirectory()) {
            return TargetInspection.refused("A file named \"" + name + "\" already exists in " + location + ".");
        }
        try {
            // A junction reads as a folder; only a folder whose real path is its own may be reused. Cloud-file
            // placeholders (OneDrive) are reparse points too, but their real path is their own.
            if (!target.toRealPath().equals(location.toRealPath().resolve(target.getFileName()))) {
                return linkRefusal(location, name);
            }
            try (DirectoryStream<Path> entries = Files.newDirectoryStream(target)) {
                if (entries.iterator().hasNext()) {
                    return TargetInspection.refused("A folder named \"" + name + "\" already exists in " + location
                            + " and isn't empty.");
                }
            }
        } catch (IOException e) {
            return TargetInspection.refused("Could not check " + target + ": " + describe(e));
        }
        return TargetInspection.ok(TargetState.EMPTY_FOLDER);
    }

    private static TargetInspection linkRefusal(Path location, String name) {
        return TargetInspection.refused("\"" + name + "\" in " + location + " is a link to another folder.");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Writing the project into the staging folder
    // -----------------------------------------------------------------------------------------------------------------

    /** Counts written files for the monitor and turns its cancel request into {@link CreationCancelled}. */
    private static final class Progress {
        private final CreationMonitor monitor;
        private final int total;
        private int done;

        Progress(CreationMonitor monitor, int total) {
            this.monitor = monitor;
            this.total = total;
        }

        void fileWritten() throws IOException {
            done++;
            monitor.progress(done, total);
            if (monitor.cancelRequested()) {
                throw new CreationCancelled();
            }
        }
    }

    private void writeProject(String name, Path projectDir, ProjectTemplate template, Progress progress)
            throws IOException {
        createDirectoryStructure(projectDir);

        createProjectFile(name, projectDir.resolve(name + ProjectValidator.PROJECT_EXTENSION));
        progress.fileWritten();
        createStartupScene(projectDir.resolve("scenes"));
        progress.fileWritten();
        createScriptProject(name, projectDir.resolve("scripts"));
        progress.fileWritten();
        createMainScript(projectDir.resolve("scripts").resolve("game"));
        progress.fileWritten();

        extractLibrary(projectDir.resolve("scripts").resolve("lib"), progress);
    }

    private void createDirectoryStructure(Path projectDir) throws IOException {
        // Inside the staging folder, which this creation made itself.
        Files.createDirectories(projectDir.resolve("scenes"));
        Files.createDirectories(projectDir.resolve("assets"));
        Files.createDirectories(projectDir.resolve("scripts").resolve("game"));
        Files.createDirectories(projectDir.resolve("scripts").resolve("compiled"));
        Files.createDirectories(projectDir.resolve("scripts").resolve("lib"));
    }

    private void createProjectFile(String projectName, Path projectFile) throws IOException {
        // Use LinkedHashMap to preserve field order in JSON
        Map<String, Object> projectConfig = new LinkedHashMap<>();
        projectConfig.put("schemaVersion", "1.0");
        projectConfig.put("projectName", projectName);
        projectConfig.put("version", "0.1.0");
        projectConfig.put("workingDirectory", ".");
        projectConfig.put("startupScene", "scenes/Main.vfScene");
        projectConfig.put("exeIconPath", "");
        projectConfig.put("engineVersion", ENGINE_VERSION);
        projectConfig.put("lastModified", formatTimestamp(clock.instant()));

        String json = gson.toJson(projectConfig);
        Files.writeString(projectFile, json);
    }

    private void createStartupScene(Path scenesDir) throws IOException {
        Path sceneFile = scenesDir.resolve("Main.vfScene");

        try (InputStream templateStream = getClass().getResourceAsStream(SCENE_TEMPLATE_RESOURCE)) {
            if (templateStream == null) {
                throw new IOException("Scene template not found: " + SCENE_TEMPLATE_RESOURCE);
            }
            Files.copy(templateStream, sceneFile);
        }
    }

    private void createScriptProject(String projectName, Path scriptsDir) throws IOException {
        Path mtprojFile = scriptsDir.resolve("scripts.mtproj");

        String xml = """
                <?xml version="1.0" encoding="UTF-8"?>
                <Project Name="%s" Version="1.0.0">
                  <Source>
                    <Include>game/**/*.mt</Include>
                  </Source>
                  <Output Directory="compiled" />
                  <Imports>
                    <SearchPath>lib</SearchPath>
                  </Imports>
                </Project>
                """.formatted(xmlAttribute(projectName));

        Files.writeString(mtprojFile, xml);
    }

    /**
     * Escapes a value for a double-quoted XML attribute. {@code '} needs no escape there and is left as it is: mType's
     * mtproj parser does not decode entities, so every escape it does not need would reach it as literal text.
     */
    static String xmlAttribute(String value) {
        StringBuilder escaped = new StringBuilder(value.length());
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            switch (c) {
                case '&' -> escaped.append("&amp;");
                case '<' -> escaped.append("&lt;");
                case '>' -> escaped.append("&gt;");
                case '"' -> escaped.append("&quot;");
                default -> escaped.append(c);
            }
        }
        return escaped.toString();
    }

    private void createMainScript(Path gameDir) throws IOException {
        Path mainScript = gameDir.resolve("Main.mt");

        // The closing delimiter sits at the content's own indentation, so no line gains a stray leading space.
        String content = """
                import * from "engine/Log.mt";
                import * from "engine/Entity.mt";

                @Script
                public class Main {
                    private int selfId;

                    constructor() {
                        // initialization code
                    }

                    public function onStart(): void {
                        this.selfId = Entity::self();
                        Log::info("Hello from Main script!");
                    }

                    public function onUpdate(float deltaTime): void {
                        // Called every frame
                    }

                    public function onFixedUpdate(float fixedDeltaTime): void {
                        // Called at fixed time intervals (after each physics step)
                    }

                    public function onLateUpdate(float deltaTime): void {
                        // Called after all onUpdate calls (e.g. camera follow)
                    }

                    public function onDestroy(): void {
                        // Called when script is destroyed
                    }
                }
                """;

        Files.writeString(mainScript, content);
    }

    private void extractLibrary(Path libDir, Progress progress) throws IOException {
        try (InputStream resourceStream = getClass().getResourceAsStream(LIBRARY_ZIP_RESOURCE)) {
            if (resourceStream == null) {
                throw ProjectTemplate.missingLibrary();
            }

            ZipExtractor.extract(resourceStream, libDir, file -> progress.fileWritten());
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Commit and cleanup
    // -----------------------------------------------------------------------------------------------------------------

    private ProjectCreationResult commit(Path location, String name, Path staging, Path target) {
        // Look again: step 5's answer may be minutes old by now (a slow network write). An empty folder that appeared
        // since is as safe to replace as one found then; anything else is refused.
        TargetInspection now = inspectTarget(location, name);
        if (now.refused()) {
            return abandon(location, name, staging, target, false, ProjectCreationResult.failure(now.problem()));
        }

        boolean deletedEmpty = false;
        try {
            if (now.state() == TargetState.EMPTY_FOLDER) {
                try {
                    // Only removes an empty folder: anything added to it since fails with DirectoryNotEmptyException.
                    TransientIo.retry(() -> Files.delete(target), CLEANUP_ATTEMPTS, CLEANUP_FIRST_DELAY,
                            CLEANUP_MAX_DELAY, "removing the empty folder " + target);
                    deletedEmpty = true;
                } catch (NoSuchFileException e) {
                    // Someone removed it in the meantime; the rename below still never replaces anything.
                }
            }
            TransientIo.retry(() -> mover.move(staging, target), COMMIT_ATTEMPTS, commitFirstDelay, commitMaxDelay,
                    "moving the new project into place");
        } catch (FileAlreadyExistsException e) {
            // When the empty folder was removed for the project, what has the name now is someone else's.
            return abandon(location, name, staging, target, deletedEmpty, ProjectCreationResult.failure(
                    "Something else created \"" + name + "\" while the project was being made. "
                            + (deletedEmpty ? "The project was not created." : "Nothing was changed.")));
        } catch (DirectoryNotEmptyException e) {
            return abandon(location, name, staging, target, deletedEmpty, ProjectCreationResult.failure(
                    "A folder named \"" + name + "\" already exists in " + location + " and isn't empty."));
        } catch (IOException | RuntimeException e) {
            return abandon(location, name, staging, target, deletedEmpty, ProjectCreationResult.failure(
                    "Could not move the new project into place: " + describe(e) + ". Another program (often an "
                            + "antivirus scan) may be using it. Try again."));
        }

        // Files.move returns quietly when source and target are the same file; make sure the rename really happened.
        if (!Files.isDirectory(target, LinkOption.NOFOLLOW_LINKS) || Files.exists(staging, LinkOption.NOFOLLOW_LINKS)) {
            return abandon(location, name, staging, target, deletedEmpty, ProjectCreationResult.failure(
                    "Could not move the new project into place: " + staging + " is still there."));
        }
        return ProjectCreationResult.success(target.resolve(name + ProjectValidator.PROJECT_EXTENSION));
    }

    /**
     * Undoes a creation that did not finish: puts back the empty folder the commit removed, then deletes the staging
     * folder. Nothing else is ever touched. Whatever could not be undone is added to {@code outcome}'s message.
     */
    private ProjectCreationResult abandon(Path location, String name, Path staging, Path target, boolean deletedEmpty,
                                          ProjectCreationResult outcome) {
        List<String> notes = new ArrayList<>();
        if (deletedEmpty) {
            try {
                Files.createDirectory(target);
            } catch (FileAlreadyExistsException e) {
                // Something else has the name again; there is nothing to put back.
            } catch (IOException e) {
                log.warn("Could not put back the empty folder " + target + ": " + describe(e));
                notes.add("The empty folder " + target + " was removed and could not be put back.");
            }
        }

        if (!deleteStaging(location, name, staging)) {
            notes.add("You can delete the temporary folder " + staging + ".");
        }
        if (notes.isEmpty()) {
            return outcome;
        }

        String note = String.join(" ", notes);
        String message = outcome.errorMessage() == null ? note : outcome.errorMessage() + " " + note;
        return new ProjectCreationResult(outcome.status(), null, message);
    }

    /**
     * Deletes the staging folder, and only after checking that it IS one: it sits in {@code location} and carries the
     * staging prefix. Links and junctions inside it are deleted themselves, never followed. Returns whether it is gone.
     */
    private boolean deleteStaging(Path location, String name, Path staging) {
        Path fileName = staging.getFileName();
        if (!location.equals(staging.getParent()) || fileName == null
                || !fileName.toString().startsWith("." + name + STAGING_INFIX)) {
            log.warn("Refusing to delete " + staging + ": it is not a staging folder in " + location);
            return false;
        }

        List<IOException> failures = new ArrayList<>();
        deleteTree(staging, failures);
        for (IOException failure : failures) {
            log.warn("Could not delete part of the temporary folder " + staging + ": " + describe(failure));
        }
        return Files.notExists(staging, LinkOption.NOFOLLOW_LINKS);
    }

    /** Deletes {@code path} and, when it is a real folder, everything in it first. Never follows a link or junction. */
    private static void deleteTree(Path path, List<IOException> failures) {
        BasicFileAttributes attributes;
        try {
            attributes = Files.readAttributes(path, BasicFileAttributes.class, LinkOption.NOFOLLOW_LINKS);
        } catch (NoSuchFileException e) {
            return;
        } catch (IOException e) {
            failures.add(e);
            return;
        }

        // A junction reads as a folder that is also "other" (a reparse point): it is deleted itself, not entered. A
        // OneDrive placeholder folder is a reparse point too, but one whose real path is its own: that one is entered.
        if (attributes.isDirectory() && (!attributes.isOther() || isOwnFolder(path))) {
            List<Path> children = List.of();
            try (Stream<Path> listing = Files.list(path)) {
                children = listing.toList();
            } catch (IOException e) {
                failures.add(e);
            }
            for (Path child : children) {
                deleteTree(child, failures);
            }
        }

        try {
            TransientIo.retry(() -> Files.deleteIfExists(path), CLEANUP_ATTEMPTS, CLEANUP_FIRST_DELAY,
                    CLEANUP_MAX_DELAY, "deleting " + path);
        } catch (IOException e) {
            failures.add(e);
        }
    }

    /** Whether {@code folder} really lives where it is listed, rather than redirecting elsewhere (a junction). */
    private static boolean isOwnFolder(Path folder) {
        Path parent = folder.getParent();
        Path name = folder.getFileName();
        if (parent == null || name == null) {
            return false;
        }
        try {
            return folder.toRealPath().equals(parent.toRealPath().resolve(name));
        } catch (IOException e) {
            return false;
        }
    }

    private String formatTimestamp(Instant instant) {
        return DateTimeFormatter.ISO_INSTANT.format(instant.atOffset(ZoneOffset.UTC));
    }

    /**
     * A short reason for the UI. Java's file exceptions often carry only the path (the Windows reason is in the type),
     * so the common types get words of their own.
     */
    static String describe(Throwable error) {
        if (error instanceof FileSystemException fileError && fileError.getReason() == null
                && fileError.getFile() != null) {
            String what = switch (fileError) {
                case NoSuchFileException ignored -> "not found";
                case AccessDeniedException ignored -> "access denied";
                case FileAlreadyExistsException ignored -> "already exists";
                case DirectoryNotEmptyException ignored -> "the folder is not empty";
                default -> null;
            };
            if (what != null) {
                return fileError.getFile() + ": " + what;
            }
        }
        if (error.getMessage() != null) {
            return error.getMessage();
        }
        return error.getClass().getSimpleName();
    }
}
