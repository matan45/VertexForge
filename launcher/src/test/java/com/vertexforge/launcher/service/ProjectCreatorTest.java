package com.vertexforge.launcher.service;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.engine.EngineContent;
import com.vertexforge.launcher.model.ProjectCreationResult;
import com.vertexforge.launcher.model.ProjectCreationResult.Status;
import com.vertexforge.launcher.template.LibraryManifest;
import com.vertexforge.launcher.template.ProjectBlueprint;
import com.vertexforge.launcher.template.TemplateScanner;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.w3c.dom.Document;

import javax.xml.parsers.DocumentBuilderFactory;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
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

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

class ProjectCreatorTest {

    private static final UUID STAGING_ID = UUID.fromString("00000000-0000-0000-0000-000000001679");
    /** The files a project of the fake engine starts with: the .vfproj, 3 template files, 3 library files, .vflib.json. */
    private static final int FILE_COUNT = 1 + TestSupport.FAKE_PAYLOAD.size() + TestSupport.FAKE_LIBRARY.size() + 1;

    @TempDir
    Path tempDir;

    /** Where the projects go. Nothing else is in it, so "the parent is untouched" can compare all of it. */
    private Path parent;
    /** The engine the projects are made from (VK-1689), outside {@link #parent}. */
    private EngineContent engine;

    @BeforeEach
    void setUp() throws IOException {
        parent = Files.createDirectory(tempDir.resolve("parent"));
        engine = TestSupport.engineContent(tempDir.resolve("engine"));
    }

    private ProjectCreator creator() {
        return creator(DirectoryMover.SYSTEM);
    }

    private ProjectCreator creator(DirectoryMover mover) {
        return new ProjectCreator(TestSupport.fixedClock(), TestSupport.log(tempDir.resolve(".logs")).log,
                () -> STAGING_ID, mover, Duration.ZERO, Duration.ZERO, TemplateScanner.SYSTEM);
    }

    private ProjectCreationResult create(String name, String... options) {
        return creator().create(TestSupport.request(name, parent, engine, options), CreationMonitor.NONE);
    }

    private ProjectCreationResult createIn(Path location, String name) {
        return creator().create(TestSupport.request(name, location, engine), CreationMonitor.NONE);
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

    /** Every file below {@code dir}, '/'-separated and relative to it. */
    private static Set<String> files(Path dir) throws IOException {
        try (Stream<Path> paths = Files.walk(dir)) {
            Set<String> files = new java.util.TreeSet<>();
            for (Path path : paths.filter(Files::isRegularFile).toList()) {
                files.add(dir.relativize(path).toString().replace('\\', '/'));
            }
            return files;
        }
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
    // What a project contains (VK-1689: the engine's template and script library)
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void createsTheTemplatesLayout() throws IOException {
        ProjectCreationResult result = create("Game");

        assertTrue(result.success(), () -> "creation failed: " + result.errorMessage());
        assertEquals(Status.CREATED, result.status());
        assertNull(result.errorMessage());
        Path projectDir = parent.resolve("Game");
        assertEquals(projectDir.resolve("Game.vfproj"), result.projectPath());
        assertEquals(Set.of("Game.vfproj", ".gitignore", "assets/scripts/scripts.mtproj", "assets/scripts/game/Main.mt",
                "assets/scripts/lib/core/Array.mt", "assets/scripts/lib/engine/Log.mt",
                "assets/scripts/lib/engine/oop/Behaviour.mt", "assets/scripts/lib/.vflib.json"), files(projectDir));
        assertFalse(Files.exists(projectDir.resolve("assets/scenes")), "the editor builds the startup scene");
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
        assertEquals("assets", json.get("workingDirectory").getAsString());
        assertEquals("scenes/Main.vfScene", json.get("startupScene").getAsString());
        assertEquals("", json.get("exeIconPath").getAsString());
        assertEquals("VertexForge 1.0.0", json.get("engineVersion").getAsString());
        assertEquals("2026-01-02T03:04:05Z", json.get("lastModified").getAsString());
    }

    @Test
    void theProjectFileCarriesTheSelectedEnginesVersion() throws IOException {
        EngineContent newer = new EngineContent(engine.engineId(), "2.3.4", engine.templatesDir(),
                engine.scriptLibDir(), engine.examplesDir(), engine.vfxLibraryDir(), "2.3.4 Release", "", false);

        ProjectCreationResult result = creator().create(TestSupport.request("Game", parent, newer),
                CreationMonitor.NONE);

        assertTrue(result.success(), result::errorMessage);
        JsonObject json = JsonParser.parseString(Files.readString(result.projectPath())).getAsJsonObject();
        assertEquals("VertexForge 2.3.4", json.get("engineVersion").getAsString());
        JsonObject library = JsonParser.parseString(Files.readString(
                parent.resolve("Game/assets/scripts/lib/.vflib.json"))).getAsJsonObject();
        assertEquals("2.3.4", library.get("engineVersion").getAsString());
    }

    @Test
    void fillsTheProjectNameIntoTheTemplatesScripts() throws IOException {
        create("Game");

        assertEquals("<Project Name=\"Game\" Version=\"1.0.0\">\n  <Output Directory=\"compiled\" />\n</Project>\n",
                Files.readString(parent.resolve("Game/assets/scripts/scripts.mtproj")));
        assertEquals("@Script\npublic class Main {\n    string greeting = \"Hello from Game!\";\n}\n",
                Files.readString(parent.resolve("Game/assets/scripts/game/Main.mt")));
    }

    @Test
    void aNameWithXmlAndMTypeSpecialCharactersIsEscapedPerFile() throws Exception {
        String name = "Tom & Jerry's {Game} $1 ü";

        ProjectCreationResult result = create(name);

        assertTrue(result.success(), result::errorMessage);
        Path mtproj = parent.resolve(name).resolve("assets/scripts/scripts.mtproj");
        DocumentBuilderFactory factory = DocumentBuilderFactory.newInstance();
        factory.setFeature("http://apache.org/xml/features/disallow-doctype-decl", true);
        Document document = factory.newDocumentBuilder().parse(mtproj.toFile());
        assertEquals(name, document.getDocumentElement().getAttribute("Name"));
        // Only what XML needs is escaped: mType reads the attribute without decoding it.
        assertTrue(Files.readString(mtproj).contains("Name=\"Tom &amp; Jerry's {Game} $1 ü\""));
        // In an mType string the braces are escaped, so even an interpolated string keeps them as text.
        assertTrue(Files.readString(parent.resolve(name).resolve("assets/scripts/game/Main.mt"))
                .contains("\"Hello from Tom & Jerry's \\{Game\\} $1 ü!\""));

        String projectFile = Files.readString(result.projectPath());
        assertEquals(name, JsonParser.parseString(projectFile).getAsJsonObject().get("projectName").getAsString());
        assertTrue(projectFile.contains("\"projectName\": \"Tom & Jerry's {Game} $1 ü\""), projectFile);
    }

    @Test
    void copiesTheScriptLibraryAndTheOtherTemplateFilesByteForByte() throws IOException {
        create("Game");

        for (Map.Entry<String, String> file : TestSupport.FAKE_LIBRARY.entrySet()) {
            assertArrayEquals(Files.readAllBytes(engine.scriptLibDir().resolve(file.getKey())),
                    Files.readAllBytes(parent.resolve("Game/assets/scripts/lib").resolve(file.getKey())), file.getKey());
        }
        assertArrayEquals(Files.readAllBytes(engine.templatesDir().resolve("empty/project/.gitignore")),
                Files.readAllBytes(parent.resolve("Game/.gitignore")));
        assertEquals("public class Array {\r\n}\r\n",
                Files.readString(parent.resolve("Game/assets/scripts/lib/core/Array.mt")), "line endings are kept");
    }

    @Test
    void writesTheLibraryManifestWithAHashOfEveryLibraryFile() throws IOException {
        create("Game");

        String text = Files.readString(parent.resolve("Game/assets/scripts/lib").resolve(LibraryManifest.FILE_NAME));
        assertFalse(text.contains("\r"), "the manifest is written with LF");
        JsonObject json = JsonParser.parseString(text).getAsJsonObject();
        assertEquals(1, json.get("formatVersion").getAsInt());
        assertEquals("1.0.0", json.get("engineVersion").getAsString());
        assertEquals("SHA-256", json.get("hashAlgorithm").getAsString());
        assertEquals("lf", json.get("lineEndings").getAsString());
        JsonObject hashes = json.getAsJsonObject("files");
        assertEquals(List.of("core/Array.mt", "engine/Log.mt", "engine/oop/Behaviour.mt"),
                new ArrayList<>(hashes.keySet()));
        for (Map.Entry<String, String> file : TestSupport.FAKE_LIBRARY.entrySet()) {
            assertEquals(LibraryManifest.hash(file.getValue().getBytes(StandardCharsets.UTF_8)),
                    hashes.get(file.getKey()).getAsString(), file.getKey());
        }
    }

    @Test
    void addsTheChosenOptionsOnly() throws IOException {
        create("Without");
        create("With", "examples", "vfxLibrary");

        assertFalse(Files.exists(parent.resolve("Without/assets/scripts/game/examples")));
        assertFalse(Files.exists(parent.resolve("Without/assets/vfx")));
        assertTrue(Files.isRegularFile(parent.resolve("With/assets/scripts/game/examples/PlayerMovement.mt")));
        assertTrue(Files.isRegularFile(parent.resolve("With/assets/vfx/library/sparks.vfVFX")));
        assertEquals(FILE_COUNT + 2, files(parent.resolve("With")).size());
    }

    @Test
    void reportsEveryFileItWrites() throws IOException {
        List<int[]> reports = new ArrayList<>();

        ProjectCreationResult result = creator().create(TestSupport.request("Game", parent, engine),
                new CreationMonitor() {
                    @Override
                    public void progress(int done, int total) {
                        reports.add(new int[]{done, total});
                    }
                });

        assertTrue(result.success(), result::errorMessage);
        assertEquals(FILE_COUNT, reports.size());
        for (int i = 0; i < reports.size(); i++) {
            assertEquals(i + 1, reports.get(i)[0]);
            assertEquals(FILE_COUNT, reports.get(i)[1]);
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Nothing is written before every check passed
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void anUnknownTemplateWritesNothing() {
        ProjectCreationResult result = creator().create(
                new CreateRequest("Game", parent, engine, "nope", Set.of()), CreationMonitor.NONE);

        assertEquals(Status.FAILED, result.status());
        assertEquals("The engine has no usable template \"nope\".", result.errorMessage());
        assertEquals(Set.of(), TestSupport.fileNames(parent));
    }

    @Test
    void anEngineWhoseLibraryIsGoneWritesNothing() throws IOException {
        EngineContent broken = new EngineContent(engine.engineId(), engine.engineVersion(), engine.templatesDir(),
                tempDir.resolve("no-such-library"), Optional.empty(), Optional.empty(), "broken", "", false);

        ProjectCreationResult result = creator().create(TestSupport.request("Game", parent, broken),
                CreationMonitor.NONE);

        assertEquals(Status.FAILED, result.status());
        assertTrue(result.errorMessage().startsWith("Could not read the engine's templates: The script library "),
                result.errorMessage());
        assertEquals(Set.of(), TestSupport.fileNames(parent));
    }

    @Test
    void anOptionTheEngineCannotProvideWritesNothing() {
        EngineContent withoutExamples = new EngineContent(engine.engineId(), engine.engineVersion(),
                engine.templatesDir(), engine.scriptLibDir(), Optional.empty(), engine.vfxLibraryDir(), "old", "",
                false);

        ProjectCreationResult result = creator().create(TestSupport.request("Game", parent, withoutExamples,
                "examples"), CreationMonitor.NONE);

        assertEquals(Status.FAILED, result.status());
        assertTrue(result.errorMessage().startsWith("\"Include examples\" cannot be added: "), result.errorMessage());
        assertEquals(Set.of(), TestSupport.fileNames(parent));
    }

    @Test
    void failsWithoutLeavingAnythingWhenTheLocationIsAFile() throws IOException {
        Path blocker = Files.writeString(parent.resolve("blocker"), "not a folder");

        ProjectCreationResult result = createIn(blocker, "Game");

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

        ProjectCreationResult result = createIn(relative, "Game");

        assertEquals(Status.FAILED, result.status());
        assertEquals(ProjectCreator.LOCATION_NOT_ABSOLUTE, result.errorMessage());
        assertFalse(Files.exists(relative), "nothing was created below the working directory");
    }

    @Test
    void aDriveRelativeLocationWritesNothing() {
        assumeTrue(TestSupport.isWindows(), "drive-relative paths are a Windows thing");

        assertEquals(ProjectCreator.LOCATION_NOT_ABSOLUTE, createIn(Path.of("C:x"), "Game").errorMessage());
        assertEquals(ProjectCreator.LOCATION_NOT_ABSOLUTE, createIn(Path.of("\\x"), "Game").errorMessage());
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
        ProjectCreationResult result = creator().create(TestSupport.request("Game", parent, engine),
                new CreationMonitor() {
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

        // File 6 is inside the script library (the .vfproj, then the 3 template files, then the library).
        ProjectCreationResult result = creator().create(TestSupport.request("Game", parent, engine), failingAt(6));

        assertEquals(Status.FAILED, result.status());
        assertTrue(result.errorMessage().contains("injected failure at file 6"), result.errorMessage());
        assertFalse(Files.exists(parent.resolve("Game")));
        assertEquals(before, snapshot(parent), "only the staging folder was ever written, and it is gone");
    }

    @Test
    void anIoExceptionMidCopyKeepsAnEmptyTargetFolder() throws IOException {
        Files.createDirectory(parent.resolve("Game"));
        Map<String, String> before = snapshot(parent);

        ProjectCreationResult result = creator().create(TestSupport.request("Game", parent, engine), failingAt(3));

        assertEquals(Status.FAILED, result.status());
        assertEquals(before, snapshot(parent));
    }

    @Test
    void aCancelMidCopyLeavesNothing() throws IOException {
        Map<String, String> before = snapshot(parent);
        AtomicBoolean cancel = new AtomicBoolean();

        ProjectCreationResult result = creator().create(TestSupport.request("Game", parent, engine),
                new CreationMonitor() {
                    @Override
                    public void progress(int done, int total) {
                        if (done == 4) {
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
    void aReadOnlyEngineFileDoesNotStopACancelledCreationFromCleaningUp() throws IOException {
        // An installed engine's files may be read-only; the copies must not be, or the staging folder could not go.
        Path readOnly = engine.scriptLibDir().resolve("core/Array.mt");
        assumeTrue(readOnly.toFile().setReadOnly(), "this file system cannot make a file read-only");
        try {
            Map<String, String> before = snapshot(parent);

            ProjectCreationResult result = creator().create(TestSupport.request("Game", parent, engine),
                    failingAt(FILE_COUNT - 1));

            assertEquals(Status.FAILED, result.status());
            assertEquals(before, snapshot(parent), "the staging folder, with its copy of the read-only file, is gone");
        } finally {
            readOnly.toFile().setWritable(true);
        }
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
        }).create(TestSupport.request("Game", parent, engine), CreationMonitor.NONE);

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
        }).create(TestSupport.request("Game", parent, engine), CreationMonitor.NONE);

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
        }).create(TestSupport.request("Game", parent, engine), CreationMonitor.NONE);

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
        }).create(TestSupport.request("Game", parent, engine), CreationMonitor.NONE);

        assertEquals(Status.FAILED, result.status());
        assertEquals("Something else created \"Game\" while the project was being made. The project was not created.",
                result.errorMessage());
        assertEquals(Set.of("intruder.txt"), TestSupport.fileNames(parent.resolve("Game")));
        assertEquals(Set.of("Game"), TestSupport.fileNames(parent), "no staging folder left");
    }

    @Test
    void theLongestAllowedPathWorksEvenThoughItsStagingCopyIsLonger() throws Exception {
        assumeTrue(TestSupport.isWindows(), "MAX_PATH is a Windows limit");
        ProjectBlueprint blueprint = TemplateScanner.SYSTEM.scan(engine).blueprint(TestSupport.TEMPLATE, Set.of());
        int deepest = blueprint.deepestRelativeLength("x");
        assertEquals("assets/scripts/lib/engine/oop/Behaviour.mt".length(), deepest);
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
        assertEquals(Optional.empty(), ProjectNameRules.checkPathLength(location, name, blueprint));

        ProjectCreationResult result = createIn(location, name);

        assertTrue(result.success(), result::errorMessage);
        // The staging folder's name (".", the name, ".creating-", a UUID) is 47 characters longer than the name, so
        // its deepest file went past 260 characters: location \ staging \ deepest.
        int stagingName = 1 + name.length() + ProjectCreator.STAGING_INFIX.length() + 36;
        assertEquals(name.length() + 47, stagingName);
        assertTrue(locationLength + 1 + stagingName + 1 + deepest > 260);
        assertEquals(Set.of(name), TestSupport.fileNames(location));
        assertEquals(blueprint.fileCount(), countFiles(location.resolve(name)));
    }

    @Test
    void oneCharacterMoreThanAllowedIsRefusedBeforeAnythingIsWritten() throws Exception {
        assumeTrue(TestSupport.isWindows(), "MAX_PATH is a Windows limit");
        ProjectBlueprint blueprint = TemplateScanner.SYSTEM.scan(engine).blueprint(TestSupport.TEMPLATE, Set.of());
        int deepest = blueprint.deepestRelativeLength("x");
        int allowed = ProjectNameRules.MAX_PATH_CHARS - ProjectNameRules.PATH_MARGIN;
        // One character longer than in the test above, so the project file is now the deepest file, one too long.
        String name = "N".repeat(deepest - ".vfproj".length() + 1);
        Path base = TestSupport.real(parent);
        // location \ name \ name.vfproj = (base + 1 + padding) + 1 + name + 1 + (deepest + 1) = allowed + 1
        int padding = allowed - deepest - name.length() - base.toString().length() - 3;
        assumeTrue(padding > 0 && padding < 200, "the temp folder's path is too long for this test");
        Path location = Files.createDirectory(base.resolve("p".repeat(padding)));

        ProjectCreationResult result = createIn(location, name);

        assertEquals(Status.FAILED, result.status());
        assertTrue(result.errorMessage().startsWith("This location and name make a path too long"),
                result.errorMessage());
        assertEquals(Set.of(), TestSupport.fileNames(location));
    }

    private static long countFiles(Path dir) throws IOException {
        try (Stream<Path> files = Files.walk(dir)) {
            return files.filter(Files::isRegularFile).count();
        }
    }
}
