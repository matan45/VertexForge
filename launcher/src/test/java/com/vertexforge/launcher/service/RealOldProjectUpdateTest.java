package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.project.EditorLockProbe;
import com.vertexforge.launcher.scriptlib.FileDecision;
import com.vertexforge.launcher.scriptlib.FileDecision.Verdict;
import com.vertexforge.launcher.scriptlib.ImportCheck;
import com.vertexforge.launcher.scriptlib.LegacyLibraryHashes;
import com.vertexforge.launcher.scriptlib.LibraryBaseline;
import com.vertexforge.launcher.service.ScriptLibraryUpdater.PreviewOutcome;
import com.vertexforge.launcher.service.ScriptLibraryUpdater.UpdateOutcome;
import com.vertexforge.launcher.template.LibraryManifest;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.UUID;
import java.util.stream.Stream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipInputStream;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

/**
 * AC (VK-1691): a project made by an older launcher, whose library is one of the libraries those launchers really
 * installed (taken from the git history), is brought to this repository's {@code assets/scripts/lib} without a single
 * conflict, and every import of its scripts still resolves afterwards. Needs the full history (skipped in a shallow
 * clone, as CI's) and {@code vertexforge.repoRoot}.
 */
class RealOldProjectUpdateTest {

    /** VK-1689's commit: the history the legacy baseline was taken from. */
    private static final String THROUGH = "9fdd282afb26bc403270a1366af7c482278a5447";
    private static final String LIB_ZIP = "launcher/src/main/resources/templates/lib.zip";
    private static final String LIZ_ZIP = "launcher/src/main/resources/templates/liz.zip";
    /** Both different {@code lib.zip}s (May 2026) ... */
    private static final List<String> LIB_ZIP_REVISIONS = List.of("980b4fec00d47803af1c3be74bc69c4b286dc6ba",
            "d3a400ebf856497f738287ad817d77d1939b8c38");
    /** ... and four of the {@code liz.zip}s (January to April 2026), whose entries the launchers took below lib/. */
    private static final List<String> LIZ_ZIP_REVISIONS = List.of("c361fa4893b05d7f25c01fb15fd27560e8adad31",
            "b42703c8dcf8a322fef06f64c3dc0fca851784b8", "3630b1f2e3a042396fbcc2ad2d7338f70e40ffcf",
            "c0efe698902b99ac98cc52dbd3860282adb57e4f");
    private static final String ENGINE_VERSION = "1.0.0";

    @TempDir
    Path tempDir;

    private Path repo;
    private Path engineLibrary;
    private int ids;

    @BeforeEach
    void setUp() throws IOException {
        String root = System.getProperty("vertexforge.repoRoot");
        assumeTrue(root != null, "vertexforge.repoRoot is not set");
        repo = Path.of(root).toAbsolutePath().normalize();
        assumeTrue(git("cat-file", "-e", THROUGH + "^{commit}").exit() == 0,
                "the git history up to " + THROUGH + " is not available");
        engineLibrary = repo.resolve("assets/scripts/lib");
        assumeTrue(Files.isDirectory(engineLibrary), "the repository has no assets/scripts/lib");
    }

    private ScriptLibraryUpdater updater() {
        return new ScriptLibraryUpdater(TestSupport.fixedClock(), target -> Long.MAX_VALUE,
                new EditorLockProbe(0, Duration.ZERO, duration -> {
                }), DirectoryMover.SYSTEM,
                () -> UUID.fromString(String.format("%08x-0000-4000-8000-000000000000", 0x20000000 + ++ids)),
                LegacyLibraryHashes::bundled, Duration.ZERO, ScriptLibraryUpdater.Checkpoint.NONE);
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Old projects
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void everyOldLaunchersLibraryUpdatesWithoutAConflictAndItsImportsStillResolve() throws Exception {
        List<String> revisions = new ArrayList<>();
        LIB_ZIP_REVISIONS.forEach(commit -> revisions.add(commit + ":" + LIB_ZIP));
        LIZ_ZIP_REVISIONS.forEach(commit -> revisions.add(commit + ":" + LIZ_ZIP));

        for (String revision : revisions) {
            Path folder = tempRoot().resolve("Old-" + revision.substring(0, 9));
            Path projectFile = oldProject(folder, zipFiles(blob(revision)));
            ScriptLibraryUpdater.Request request = new ScriptLibraryUpdater.Request(projectFile, ".", engineLibrary,
                    ENGINE_VERSION);

            ScriptLibraryUpdater.Preview preview = ready(updater().preview(request, JobMonitor.NONE));

            assertInstanceOf(LibraryBaseline.Legacy.class, preview.baseline(), revision);
            for (FileDecision decision : preview.plan().decisions()) {
                assertTrue(!decision.verdict().userChanged(), revision + ": " + decision);
            }
            assertEquals(0, preview.conflicts(), revision);
            assertEquals(List.of(), preview.brokenImports(), revision + ": " + preview.brokenImports());

            UpdateOutcome outcome = updater().update(request, preview.fingerprint(), false, JobMonitor.NONE);
            assertInstanceOf(UpdateOutcome.Done.class, outcome, () -> revision + ": " + outcome);

            Path scripts = folder.resolve("scripts");
            assertEquals(List.of(), unresolvedImports(scripts), revision);
            assertEquals(libraryFiles(engineLibrary), libraryFiles(scripts.resolve("lib")), revision);
            assertTrue(ready(updater().preview(request, JobMonitor.NONE)).plan().upToDate(), revision);
        }
    }

    /**
     * {@code engine/Entity.mt} had many versions: an edit of it can be told from none of them, so it is a conflict;
     * {@code engine/Log.mt} has only ever had the engine's current version, so an edit of it is the user's alone and
     * is kept (the legacy table's {@code K = {e}} row).
     */
    @Test
    void anEditedFileOfAnOldLibraryIsAConflictOrKeptAndItsOldBytesAreInTheBackup() throws Exception {
        LegacyLibraryHashes known = LegacyLibraryHashes.bundled();
        assumeTrue(known.hashes(LibraryManifest.pathKey("engine/Entity.mt")).size() > 1
                && known.hashes(LibraryManifest.pathKey("engine/Log.mt")).equals(java.util.Set.of(
                LibraryManifest.hash(Files.readAllBytes(engineLibrary.resolve("engine/Log.mt"))))),
                "the engine's Log.mt or Entity.mt history changed");
        Path folder = tempRoot().resolve("Edited");
        Path projectFile = oldProject(folder, zipFiles(blob(LIB_ZIP_REVISIONS.getFirst() + ":" + LIB_ZIP)));
        Path entity = folder.resolve("scripts/lib/engine/Entity.mt");
        Path log = folder.resolve("scripts/lib/engine/Log.mt");
        byte[] editedEntity = (Files.readString(entity, StandardCharsets.UTF_8) + "\n// my own change\n")
                .getBytes(StandardCharsets.UTF_8);
        byte[] editedLog = (Files.readString(log, StandardCharsets.UTF_8) + "\n// my own change\n")
                .getBytes(StandardCharsets.UTF_8);
        Files.write(entity, editedEntity);
        Files.write(log, editedLog);
        ScriptLibraryUpdater.Request request = new ScriptLibraryUpdater.Request(projectFile, ".", engineLibrary,
                ENGINE_VERSION);

        ScriptLibraryUpdater.Preview preview = ready(updater().preview(request, JobMonitor.NONE));

        assertEquals(List.of("engine/Entity.mt"), preview.plan().decisions(Verdict.CONFLICT).stream()
                .map(FileDecision::displayPath).toList());
        assertEquals(List.of("engine/Log.mt"), preview.plan().decisions(Verdict.KEEP_EDIT).stream()
                .map(FileDecision::displayPath).toList());
        UpdateOutcome outcome = updater().update(request, preview.fingerprint(), false, JobMonitor.NONE);
        UpdateOutcome.Done done = assertInstanceOf(UpdateOutcome.Done.class, outcome, outcome::toString);
        try (ZipFile zip = new ZipFile(done.backup().toFile())) {
            try (InputStream in = zip.getInputStream(zip.getEntry("lib/engine/Entity.mt"))) {
                assertArrayEquals(editedEntity, in.readAllBytes());
            }
        }
        assertArrayEquals(Files.readAllBytes(engineLibrary.resolve("engine/Entity.mt")), Files.readAllBytes(entity),
                "the conflict takes the engine's copy");
        assertArrayEquals(editedLog, Files.readAllBytes(log), "the user's own change is kept");
        assertTrue(ready(updater().preview(request, JobMonitor.NONE)).plan().upToDate());
    }

    @Test
    void aProjectMadeAsTheLauncherNowMakesOneIsUpToDateAtOnce() throws Exception {
        Path folder = Files.createDirectories(tempRoot().resolve("New"));
        Path projectFile = Files.writeString(folder.resolve("New.vfproj"), TestSupport.projectJson("New", "1.0",
                "VertexForge 1.0.0").replace("\"workingDirectory\": \".\"", "\"workingDirectory\": \"assets\""));
        assertTrue(Files.readString(projectFile).contains("\"workingDirectory\": \"assets\""));
        Path lib = folder.resolve("assets/scripts/lib");
        Map<String, String> hashes = new TreeMap<>();
        try (Stream<Path> files = Files.walk(engineLibrary)) {
            for (Path file : files.filter(Files::isRegularFile).toList()) {
                String relative = engineLibrary.relativize(file).toString().replace('\\', '/');
                byte[] bytes = Files.readAllBytes(file);
                Path target = lib.resolve(relative);
                Files.createDirectories(target.getParent());
                Files.write(target, bytes);
                if (LibraryManifest.isLibraryFile(relative)) {
                    hashes.put(relative, LibraryManifest.hash(bytes));
                }
            }
        }
        Files.write(lib.resolve(LibraryManifest.FILE_NAME), LibraryManifest.toJson(ENGINE_VERSION, hashes));
        ScriptLibraryUpdater.Request request = new ScriptLibraryUpdater.Request(projectFile, "assets", engineLibrary,
                ENGINE_VERSION);

        ScriptLibraryUpdater.Preview preview = ready(updater().preview(request, JobMonitor.NONE));

        assertTrue(preview.plan().upToDate(), preview.plan().decisions().stream()
                .filter(decision -> decision.verdict() != Verdict.NOOP).toList().toString());
        assertInstanceOf(UpdateOutcome.UpToDate.class,
                updater().update(request, preview.fingerprint(), false, JobMonitor.NONE));
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------------------------------------------------

    private Path tempRoot() {
        return TestSupport.real(tempDir);
    }

    private static ScriptLibraryUpdater.Preview ready(PreviewOutcome outcome) {
        return assertInstanceOf(PreviewOutcome.Ready.class, outcome, outcome::toString).preview();
    }

    /**
     * A project as the launchers of January to November 2026 made one: working directory ".", a scripts project with
     * the search path {@code lib}, a game script importing two library files, and the zip's files below
     * {@code scripts/lib} (a {@code lib/} prefix stripped, as those launchers did).
     */
    private static Path oldProject(Path folder, Map<String, byte[]> zip) throws IOException {
        Path projectFile = TestSupport.writeProject(folder, folder.getFileName().toString());
        Path scripts = folder.resolve("scripts");
        Files.createDirectories(scripts.resolve("game"));
        Files.writeString(scripts.resolve("scripts.mtproj"), "<Project Name=\"Old\" Version=\"1.0.0\">\n"
                + "  <Output Directory=\"compiled\" />\n  <SearchPath>lib</SearchPath>\n</Project>\n");
        Files.writeString(scripts.resolve("game/Main.mt"), "import * from \"engine/Log.mt\";\n"
                + "import * from \"engine/Entity.mt\";\n@Script\npublic class Main {\n}\n");
        for (Map.Entry<String, byte[]> file : zip.entrySet()) {
            String name = file.getKey().startsWith("lib/") ? file.getKey().substring(4) : file.getKey();
            Path target = scripts.resolve("lib").resolve(name);
            Files.createDirectories(target.getParent());
            Files.write(target, file.getValue());
        }
        assertTrue(Files.isRegularFile(scripts.resolve("lib/engine/Log.mt")));
        return projectFile;
    }

    /** Every file of the library, by relative path, to its bytes' hash (the record included). */
    private static Map<String, String> libraryFiles(Path library) throws IOException {
        Map<String, String> files = new TreeMap<>();
        try (Stream<Path> paths = Files.walk(library)) {
            for (Path path : paths.filter(Files::isRegularFile).toList()) {
                String relative = library.relativize(path).toString().replace('\\', '/');
                if (LibraryManifest.isLibraryFile(relative)) {
                    files.put(relative, LibraryManifest.hash(Files.readAllBytes(path)));
                }
            }
        }
        return files;
    }

    /**
     * Every import of every script below {@code scripts/game} and {@code scripts/lib} that mType's resolver would not
     * find (the rules of {@code test_repo_scan_helpers.hpp}'s {@code importResolves}, search path {@code lib}).
     */
    private static List<String> unresolvedImports(Path scripts) throws IOException {
        List<String> failures = new ArrayList<>();
        for (String folder : List.of("game", "lib")) {
            try (Stream<Path> paths = Files.walk(scripts.resolve(folder))) {
                for (Path script : paths.filter(path -> path.toString().endsWith(".mt")).toList()) {
                    for (String target : ImportCheck.imports(Files.readString(script, StandardCharsets.UTF_8))) {
                        if (!resolves(scripts, script, target)) {
                            failures.add(scripts.relativize(script) + " -> " + target);
                        }
                    }
                }
            }
        }
        return failures;
    }

    private static boolean resolves(Path scripts, Path script, String target) {
        Path relative;
        try {
            relative = Path.of(target);
        } catch (InvalidPathException e) {
            return false;
        }
        if (relative.isAbsolute()) {
            return Files.exists(relative);
        }
        return Files.exists(script.getParent().resolve(relative)) || Files.exists(scripts.resolve(relative))
                || Files.exists(scripts.resolve("lib").resolve(relative));
    }

    private static Map<String, byte[]> zipFiles(byte[] zip) throws IOException {
        Map<String, byte[]> files = new TreeMap<>();
        try (ZipInputStream in = new ZipInputStream(new ByteArrayInputStream(zip), StandardCharsets.UTF_8)) {
            for (ZipEntry entry = in.getNextEntry(); entry != null; entry = in.getNextEntry()) {
                if (!entry.isDirectory()) {
                    files.put(entry.getName(), in.readAllBytes());
                }
            }
        }
        assertTrue(files.size() > 50, "a whole library: " + files.size());
        return files;
    }

    private byte[] blob(String revision) throws IOException {
        Result result = git("cat-file", "blob", revision);
        assertEquals(0, result.exit(), "git cat-file blob " + revision);
        return result.out();
    }

    private record Result(int exit, byte[] out) {
    }

    /** Runs git in the repository, bytes as stored (no line-ending conversion). */
    private Result git(String... args) throws IOException {
        List<String> command = new ArrayList<>(List.of("git", "-C", repo.toString(), "-c", "core.autocrlf=false"));
        command.addAll(List.of(args));
        Process process;
        try {
            process = new ProcessBuilder(command).redirectError(ProcessBuilder.Redirect.DISCARD).start();
        } catch (IOException e) {
            assumeTrue(false, "git is not available: " + e.getMessage());
            throw e;
        }
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        try (InputStream in = process.getInputStream()) {
            in.transferTo(out);
        }
        try {
            return new Result(process.waitFor(), out.toByteArray());
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("interrupted while running git", e);
        }
    }
}
