package com.vertexforge.launcher.service;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.model.ProjectCreationResult;
import com.vertexforge.launcher.model.ProjectCreationResult.Status;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.w3c.dom.Document;

import javax.xml.parsers.DocumentBuilderFactory;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.AccessDeniedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.TreeMap;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Stream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

class ProjectCreatorTest {

    private static final UUID STAGING_ID = UUID.fromString("00000000-0000-0000-0000-000000001679");

    @TempDir
    Path tempDir;

    /** Where the projects go. Nothing else is in it, so "the parent is untouched" can compare all of it. */
    private Path parent;

    @BeforeEach
    void setUp() throws IOException {
        parent = Files.createDirectory(tempDir.resolve("parent"));
    }

    private ProjectCreator creator() {
        return creator(DirectoryMover.SYSTEM);
    }

    private ProjectCreator creator(DirectoryMover mover) {
        return new ProjectCreator(TestSupport.fixedClock(), TestSupport.log(tempDir.resolve(".logs")).log,
                () -> STAGING_ID, mover, Duration.ZERO, Duration.ZERO, ProjectTemplate.BUNDLED);
    }

    private ProjectCreationResult create(String name) {
        return creator().createProject(name, parent);
    }

    private static byte[] resource(String name) throws IOException {
        try (InputStream in = ProjectCreatorTest.class.getResourceAsStream(name)) {
            assertNotNull(in, "missing classpath resource " + name);
            return in.readAllBytes();
        }
    }

    /** Entry name (always '/'-separated) to bytes, files only. */
    static Map<String, byte[]> zipEntries(byte[] zip) throws IOException {
        Map<String, byte[]> entries = new TreeMap<>();
        try (ZipInputStream in = new ZipInputStream(new java.io.ByteArrayInputStream(zip))) {
            ZipEntry entry;
            while ((entry = in.getNextEntry()) != null) {
                if (!entry.isDirectory()) {
                    entries.put(entry.getName(), in.readAllBytes());
                }
            }
        }
        return entries;
    }

    /** Every folder and file below {@code dir}, with each file's bytes: what "untouched" compares. */
    private static Map<String, String> snapshot(Path dir) throws IOException {
        Map<String, String> tree = new TreeMap<>();
        try (Stream<Path> paths = Files.walk(dir)) {
            for (Path path : paths.toList()) {
                String key = dir.relativize(path).toString();
                tree.put(key, Files.isDirectory(path) ? "<dir>" : HexFormat.of().formatHex(Files.readAllBytes(path)));
            }
        }
        return tree;
    }

    /** A monitor whose {@code progress} throws once {@code done} reaches {@code failAt}. */
    private static CreationMonitor failingAt(int failAt) {
        return new CreationMonitor() {
            @Override
            public void progress(int done, int total) throws IOException {
                if (done == failAt) {
                    throw new IOException("injected failure at file " + failAt);
                }
            }
        };
    }

    // -----------------------------------------------------------------------------------------------------------------
    // What a project contains
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void createsTheFolderTree() {
        ProjectCreationResult result = create("Game");

        assertTrue(result.success(), () -> "creation failed: " + result.errorMessage());
        assertEquals(Status.CREATED, result.status());
        assertNull(result.errorMessage());
        Path projectDir = parent.resolve("Game");
        assertEquals(projectDir.resolve("Game.vfproj"), result.projectPath());
        for (String dir : List.of("scenes", "assets", "scripts", "scripts/game", "scripts/compiled", "scripts/lib")) {
            assertTrue(Files.isDirectory(projectDir.resolve(dir)), dir + " is missing");
        }
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent), "the staging folder was renamed, not left");
    }

    @Test
    void writesTheProjectFileFieldsInOrder() throws IOException {
        Path projectFile = create("Game").projectPath();

        JsonObject json = JsonParser.parseString(Files.readString(projectFile)).getAsJsonObject();

        assertEquals(List.of("schemaVersion", "projectName", "version", "workingDirectory", "startupScene",
                "exeIconPath", "engineVersion", "lastModified"), new ArrayList<>(json.keySet()));
        assertEquals("1.0", json.get("schemaVersion").getAsString());
        assertEquals("Game", json.get("projectName").getAsString());
        assertEquals("0.1.0", json.get("version").getAsString());
        assertEquals(".", json.get("workingDirectory").getAsString());
        assertEquals("scenes/Main.vfScene", json.get("startupScene").getAsString());
        assertEquals("", json.get("exeIconPath").getAsString());
        assertEquals("VertexForge 1.0.0", json.get("engineVersion").getAsString());
        assertEquals("2026-01-02T03:04:05Z", json.get("lastModified").getAsString());
    }

    @Test
    void copiesTheSceneTemplateByteForByte() throws IOException {
        create("Game");

        assertArrayEquals(resource("/templates/Main.vfScene"),
                Files.readAllBytes(parent.resolve("Game/scenes/Main.vfScene")));
    }

    @Test
    void writesTheScriptProject() throws IOException {
        create("Game");

        String expected = """
                <?xml version="1.0" encoding="UTF-8"?>
                <Project Name="Game" Version="1.0.0">
                  <Source>
                    <Include>game/**/*.mt</Include>
                  </Source>
                  <Output Directory="compiled" />
                  <Imports>
                    <SearchPath>lib</SearchPath>
                  </Imports>
                </Project>
                """;
        assertEquals(expected, Files.readString(parent.resolve("Game/scripts/scripts.mtproj")));
    }

    @Test
    void aNameWithAnAmpersandAndAnApostropheGivesValidXmlAndJson() throws Exception {
        String name = "Tom & Jerry's Game";

        ProjectCreationResult result = create(name);

        assertTrue(result.success(), result::errorMessage);
        Path mtproj = parent.resolve(name).resolve("scripts/scripts.mtproj");
        DocumentBuilderFactory factory = DocumentBuilderFactory.newInstance();
        factory.setFeature("http://apache.org/xml/features/disallow-doctype-decl", true);
        Document document = factory.newDocumentBuilder().parse(mtproj.toFile());
        assertEquals(name, document.getDocumentElement().getAttribute("Name"));
        // Only what XML needs is escaped: mType reads the attribute without decoding it.
        assertTrue(Files.readString(mtproj).contains("Name=\"Tom &amp; Jerry's Game\""));

        String projectFile = Files.readString(result.projectPath());
        assertEquals(name, JsonParser.parseString(projectFile).getAsJsonObject().get("projectName").getAsString());
        assertTrue(projectFile.contains("\"projectName\": \"Tom & Jerry's Game\""), projectFile);
    }

    @Test
    void escapesEveryCharacterAnXmlAttributeNeeds() {
        assertEquals("a &amp; b &lt;c&gt; &quot;d&quot; 'e'", ProjectCreator.xmlAttribute("a & b <c> \"d\" 'e'"));
    }

    @Test
    void writesTheMainScriptWithoutAStrayLeadingSpace() throws IOException {
        create("Game");

        String script = Files.readString(parent.resolve("Game/scripts/game/Main.mt"));

        String expected = """
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
        assertEquals(expected, script);
        for (String line : script.lines().toList()) {
            int indent = line.length() - line.stripLeading().length();
            assertEquals(0, indent % 4, () -> "indented by " + indent + ": [" + line + "]");
            assertFalse(!line.isEmpty() && line.isBlank(), "a line holding only spaces");
        }
        assertTrue(script.startsWith("import"));
        assertTrue(script.endsWith("}\n"));
    }

    @Test
    void extractsTheWholeScriptLibrary() throws IOException {
        create("Game");
        Path libDir = parent.resolve("Game/scripts/lib");
        Map<String, byte[]> expected = zipEntries(resource("/templates/script-library.zip"));

        List<Path> extracted;
        try (Stream<Path> files = Files.walk(libDir)) {
            extracted = files.filter(Files::isRegularFile).toList();
        }

        assertFalse(expected.isEmpty());
        assertEquals(expected.size(), extracted.size());
        for (Map.Entry<String, byte[]> entry : expected.entrySet()) {
            Path file = libDir.resolve(entry.getKey());
            assertTrue(Files.isRegularFile(file), entry.getKey() + " was not extracted");
            assertArrayEquals(entry.getValue(), Files.readAllBytes(file), entry.getKey());
        }
    }

    @Test
    void reportsEveryFileItWrites() throws IOException {
        List<int[]> reports = new ArrayList<>();

        ProjectCreationResult result = creator().create(new CreateRequest("Game", parent), new CreationMonitor() {
            @Override
            public void progress(int done, int total) {
                reports.add(new int[]{done, total});
            }
        });

        assertTrue(result.success(), result::errorMessage);
        int total = ProjectTemplate.bundled().fileCount();
        assertEquals(total, reports.size());
        for (int i = 0; i < reports.size(); i++) {
            assertEquals(i + 1, reports.get(i)[0]);
            assertEquals(total, reports.get(i)[1]);
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Nothing is written before every check passed
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void failsWithoutLeavingAnythingWhenTheLocationIsAFile() throws IOException {
        Path blocker = Files.writeString(parent.resolve("blocker"), "not a folder");

        ProjectCreationResult result = creator().createProject("Game", blocker);

        assertFalse(result.success());
        assertEquals(Status.FAILED, result.status());
        assertNull(result.projectPath());
        assertTrue(result.errorMessage().endsWith("is not a folder."), result.errorMessage());
        assertEquals("not a folder", Files.readString(blocker));
        assertEquals(Set.of("blocker"), TestSupport.fileNames(parent));
    }

    @Test
    void aRelativeLocationWritesNothing() {
        Path relative = Path.of("vk1679-relative-" + UUID.randomUUID());

        ProjectCreationResult result = creator().createProject("Game", relative);

        assertEquals(Status.FAILED, result.status());
        assertEquals(ProjectCreator.LOCATION_NOT_ABSOLUTE, result.errorMessage());
        assertFalse(Files.exists(relative), "nothing was created below the working directory");
    }

    @Test
    void aDriveRelativeLocationWritesNothing() {
        assumeTrue(TestSupport.isWindows(), "drive-relative paths are a Windows thing");

        assertEquals(ProjectCreator.LOCATION_NOT_ABSOLUTE, creator().createProject("Game", Path.of("C:x")).errorMessage());
        assertEquals(ProjectCreator.LOCATION_NOT_ABSOLUTE, creator().createProject("Game", Path.of("\\x")).errorMessage());
    }

    @Test
    void aBadNameWritesNothing() {
        for (String name : List.of("CON", "aux.txt", "Game.", "Game ", " Game")) {
            ProjectCreationResult result = create(name);

            assertEquals(Status.FAILED, result.status(), name);
            assertEquals(ProjectNameRules.checkName(name).orElseThrow().message(), result.errorMessage());
        }
        assertEquals(Set.of(), TestSupport.fileNames(parent));
    }

    @Test
    void aNonEmptyTargetFolderIsRefusedAndLeftAsItWas() throws IOException {
        Path existing = Files.createDirectory(parent.resolve("Game"));
        Files.writeString(existing.resolve("notes.txt"), "mine");
        Map<String, String> before = snapshot(parent);

        ProjectCreationResult result = create("Game");

        assertEquals(Status.FAILED, result.status());
        assertTrue(result.errorMessage().contains("already exists") && result.errorMessage().contains("isn't empty"),
                result.errorMessage());
        assertEquals(before, snapshot(parent));
    }

    @Test
    void aFileWithTheProjectsNameIsRefused() throws IOException {
        Files.writeString(parent.resolve("Game"), "a file");
        Map<String, String> before = snapshot(parent);

        ProjectCreationResult result = create("Game");

        assertEquals(Status.FAILED, result.status());
        assertTrue(result.errorMessage().startsWith("A file named \"Game\" already exists"), result.errorMessage());
        assertEquals(before, snapshot(parent));
    }

    @Test
    void anEmptyTargetFolderIsUsed() throws IOException {
        Files.createDirectory(parent.resolve("Game"));

        ProjectCreationResult result = create("Game");

        assertTrue(result.success(), result::errorMessage);
        assertTrue(Files.isRegularFile(parent.resolve("Game/Game.vfproj")));
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
    }

    @Test
    void aLinkWithTheProjectsNameIsRefused() throws IOException {
        Path elsewhere = Files.createDirectory(tempDir.resolve("elsewhere"));
        try {
            Files.createSymbolicLink(parent.resolve("Game"), elsewhere);
        } catch (IOException | UnsupportedOperationException | SecurityException e) {
            assumeTrue(false, "this machine cannot create symbolic links: " + e);
        }

        ProjectCreationResult result = create("Game");

        assertEquals(Status.FAILED, result.status());
        assertTrue(result.errorMessage().endsWith("is a link to another folder."), result.errorMessage());
        assertEquals(Set.of(), TestSupport.fileNames(elsewhere));
        assertTrue(Files.isSymbolicLink(parent.resolve("Game")));
    }

    @Test
    void aCancelBeforeAnythingIsWrittenWritesNothing() {
        ProjectCreationResult result = creator().create(new CreateRequest("Game", parent), new CreationMonitor() {
            @Override
            public boolean cancelRequested() {
                return true;
            }
        });

        assertEquals(Status.CANCELLED, result.status());
        assertNull(result.errorMessage());
        assertEquals(Set.of(), TestSupport.fileNames(parent));
    }

    // -----------------------------------------------------------------------------------------------------------------
    // A failure or a cancel half way leaves nothing behind
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void anIoExceptionMidCopyLeavesNoTargetAndTheParentUntouched() throws IOException {
        Files.writeString(parent.resolve("keep.txt"), "someone else's file");
        Files.createDirectory(parent.resolve("Other"));
        Map<String, String> before = snapshot(parent);

        // File 50 is deep inside the script library.
        ProjectCreationResult result = creator().create(new CreateRequest("Game", parent), failingAt(50));

        assertEquals(Status.FAILED, result.status());
        assertTrue(result.errorMessage().contains("injected failure at file 50"), result.errorMessage());
        assertFalse(Files.exists(parent.resolve("Game")));
        assertEquals(before, snapshot(parent), "only the staging folder was ever written, and it is gone");
    }

    @Test
    void anIoExceptionMidCopyKeepsAnEmptyTargetFolder() throws IOException {
        Files.createDirectory(parent.resolve("Game"));
        Map<String, String> before = snapshot(parent);

        ProjectCreationResult result = creator().create(new CreateRequest("Game", parent), failingAt(20));

        assertEquals(Status.FAILED, result.status());
        assertEquals(before, snapshot(parent));
    }

    @Test
    void aCancelMidCopyLeavesNothing() throws IOException {
        Map<String, String> before = snapshot(parent);
        AtomicBoolean cancel = new AtomicBoolean();

        ProjectCreationResult result = creator().create(new CreateRequest("Game", parent), new CreationMonitor() {
            @Override
            public void progress(int done, int total) {
                if (done == 10) {
                    cancel.set(true);
                }
            }

            @Override
            public boolean cancelRequested() {
                return cancel.get();
            }
        });

        assertEquals(Status.CANCELLED, result.status());
        assertNull(result.errorMessage());
        assertNull(result.projectPath());
        assertEquals(before, snapshot(parent));
    }

    @Test
    void aFailedRenameLeavesNoStagingFolderAndPutsBackTheEmptyTarget() throws IOException {
        Files.createDirectory(parent.resolve("Game"));
        Map<String, String> before = snapshot(parent);
        AtomicInteger calls = new AtomicInteger();

        ProjectCreationResult result = creator((source, target) -> {
            calls.incrementAndGet();
            assertEquals(parent.resolve(".Game" + ProjectCreator.STAGING_INFIX + STAGING_ID), source);
            throw new IOException("the disk is on fire");
        }).createProject("Game", parent);

        assertEquals(Status.FAILED, result.status());
        assertTrue(result.errorMessage().startsWith("Could not move the new project into place: the disk is on fire"),
                result.errorMessage());
        assertEquals(1, calls.get(), "a failure that is not transient is not retried");
        assertEquals(before, snapshot(parent));
    }

    @Test
    void aRenameBlockedForAMomentIsRetried() {
        AtomicInteger calls = new AtomicInteger();

        ProjectCreationResult result = creator((source, target) -> {
            if (calls.incrementAndGet() < 3) {
                throw new AccessDeniedException(source.toString(), null, "used by another process");
            }
            DirectoryMover.SYSTEM.move(source, target);
        }).createProject("Game", parent);

        assertTrue(result.success(), result::errorMessage);
        assertEquals(3, calls.get());
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
    }

    @Test
    void aTargetThatAppearsJustBeforeTheRenameIsNeverReplaced() throws IOException {
        ProjectCreationResult result = creator((source, target) -> {
            Files.createDirectory(target);
            Files.writeString(target.resolve("intruder.txt"), "theirs");
            DirectoryMover.SYSTEM.move(source, target);
        }).createProject("Game", parent);

        assertEquals(Status.FAILED, result.status());
        assertEquals("Something else created \"Game\" while the project was being made. Nothing was changed.",
                result.errorMessage());
        assertEquals("theirs", Files.readString(parent.resolve("Game/intruder.txt")));
        assertEquals(Set.of("intruder.txt"), TestSupport.fileNames(parent.resolve("Game")));
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent));
    }

    @Test
    void aTargetTakenAfterItsEmptyFolderWasRemovedKeepsTheNewcomer() throws IOException {
        Files.createDirectory(parent.resolve("Game"));

        ProjectCreationResult result = creator((source, target) -> {
            // The empty folder is already gone; something else takes the name before the rename.
            Files.createDirectory(target);
            Files.writeString(target.resolve("intruder.txt"), "theirs");
            DirectoryMover.SYSTEM.move(source, target);
        }).createProject("Game", parent);

        assertEquals(Status.FAILED, result.status());
        assertEquals("Something else created \"Game\" while the project was being made. The project was not created.",
                result.errorMessage());
        assertEquals(Set.of("intruder.txt"), TestSupport.fileNames(parent.resolve("Game")));
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent), "no staging folder left");
    }

    @Test
    void theLongestAllowedPathWorksEvenThoughItsStagingCopyIsLonger() throws IOException {
        assumeTrue(TestSupport.isWindows(), "MAX_PATH is a Windows limit");
        ProjectTemplate template = ProjectTemplate.bundled();
        int deepest = template.deepestRelativeLength("x");
        int allowed = ProjectNameRules.MAX_PATH_CHARS - ProjectNameRules.PATH_MARGIN;
        // The longest name whose project file is not deeper than the deepest template file, and a location that makes
        // the deepest file exactly as long as allowed: location \ name \ deepest.
        String name = "N".repeat(deepest - ".vfproj".length());
        Path base = TestSupport.real(parent);
        int locationLength = allowed - deepest - name.length() - 2;
        int padding = locationLength - base.toString().length() - 1;
        assumeTrue(padding > 0 && padding < 200, "the temp folder's path is too long for this test");
        Path location = Files.createDirectory(base.resolve("p".repeat(padding)));
        assertEquals(locationLength, location.toString().length());
        assertEquals(Optional.empty(), ProjectNameRules.checkPathLength(location, name, template));

        ProjectCreationResult result = creator().createProject(name, location);

        assertTrue(result.success(), result::errorMessage);
        // The staging folder's name (".", the name, ".creating-", a UUID) is 47 characters longer than the name, so
        // its deepest file went past 260 characters: location \ staging \ deepest.
        int stagingName = 1 + name.length() + ProjectCreator.STAGING_INFIX.length() + 36;
        assertEquals(name.length() + 47, stagingName);
        assertTrue(locationLength + 1 + stagingName + 1 + deepest > 260);
        assertEquals(Set.of(name), TestSupport.fileNames(location));
        assertEquals(template.fileCount(), countFiles(location.resolve(name)));
    }

    private static long countFiles(Path dir) throws IOException {
        try (Stream<Path> files = Files.walk(dir)) {
            return files.filter(Files::isRegularFile).count();
        }
    }
}
