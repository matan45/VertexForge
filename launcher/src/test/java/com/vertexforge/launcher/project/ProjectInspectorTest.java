package com.vertexforge.launcher.project;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.model.ProjectCreationResult;
import com.vertexforge.launcher.service.ProjectCreator;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;
import org.junit.jupiter.api.io.TempDir;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Locale;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

class ProjectInspectorTest {

    private static final String VALID = "{\"projectName\": \"My Game\", \"schemaVersion\": \"1.0\","
            + " \"engineVersion\": \"VertexForge 1.0.0\"}";

    @TempDir
    Path tempDir;

    private final ProjectInspector inspector = ProjectInspector.system();

    private static Path project(Path dir, String fileName, String text) throws IOException {
        return Files.writeString(Files.createDirectories(dir).resolve(fileName), text);
    }

    private PickedProject.Found found(Path picked) {
        PickedProject result = inspector.inspect(picked);
        return assertInstanceOf(PickedProject.Found.class, result, result::toString);
    }

    private PickedProject.Refused refused(Path picked) {
        PickedProject result = inspector.inspect(picked);
        return assertInstanceOf(PickedProject.Refused.class, result, result::toString);
    }

    @Test
    void aProjectFileIsFoundUnderItsRealPathWithWhatItSays() throws IOException {
        Path file = project(tempDir.resolve("Game"), "Game.vfproj", VALID);

        PickedProject.Found found = found(file);

        assertEquals(TestSupport.real(file).toString(), found.file().toString());
        assertEquals(found.file(), found.picked());
        assertEquals(new ProjectFile("My Game", "1.0", "VertexForge 1.0.0"), found.project());
    }

    @ParameterizedTest
    @ValueSource(strings = {"Game.VFPROJ", "Game.VfProj"})
    void theExtensionMayBeInAnyLetterCase(String fileName) throws IOException {
        Path file = project(tempDir.resolve("Game"), fileName, VALID);

        assertEquals("My Game", found(file).project().projectName());
    }

    @Test
    void aProjectTheLauncherCreatedIsFoundFromItsFolder() throws IOException {
        Path parent = Files.createDirectory(tempDir.resolve("parent"));
        ProjectCreationResult created = new ProjectCreator(TestSupport.fixedClock(),
                TestSupport.log(tempDir.resolve(".logs")).log).createProject("Tom & Jerry's Game", parent);
        assertTrue(created.success(), created::errorMessage);

        PickedProject.Found found = found(created.projectPath().getParent());

        assertEquals(TestSupport.real(created.projectPath()), found.file());
        assertEquals("Tom & Jerry's Game", found.project().projectName());
    }

    @Test
    void aFileWithAnotherExtensionIsRefused() throws IOException {
        Path file = project(tempDir, "Game.txt", VALID);

        PickedProject.Refused refused = refused(file);

        assertEquals("Is not a .vfproj file", refused.reason());
        assertEquals(file, refused.picked());
    }

    @Test
    void aFileNamedOnlyByTheExtensionIsRefused() throws IOException {
        assertEquals("Is not a .vfproj file", refused(project(tempDir, ".vfproj", VALID)).reason());
    }

    @Test
    void anInvalidProjectFileIsRefusedWithItsParseError() throws IOException {
        Path file = project(tempDir, "Broken.vfproj", "{\n  \"projectName\": \"A\",\n  \"x\" 1\n}");

        PickedProject.Refused refused = refused(file);

        assertEquals("Not valid JSON at line 3, column 8: expected ':'", refused.reason());
        assertEquals(file, refused.picked());
    }

    @Test
    void aMissingFileIsRefused() {
        Path missing = tempDir.resolve("gone").resolve("Gone.vfproj");

        assertEquals("Was not found", refused(missing).reason());
    }

    @Test
    @EnabledOnOs(OS.WINDOWS)
    void aProjectOnAnAbsentDriveIsRefusedAndSaysWhichDrive() {
        Optional<Path> absentRoot = TestSupport.absentDriveRoot();
        assumeTrue(absentRoot.isPresent(), "every drive letter from D to Z is in use");

        PickedProject.Refused refused = refused(absentRoot.get().resolve("Projects").resolve("Game.vfproj"));

        assertEquals("Drive " + absentRoot.get() + " is not available", refused.reason());
    }

    @Test
    void aFolderWithOneProjectFileGivesThatProject() throws IOException {
        Path folder = tempDir.resolve("One");
        Path file = project(folder, "Game.vfproj", VALID);

        assertEquals(TestSupport.real(file), found(folder).file());
    }

    @Test
    void onlyProjectFilesDirectlyInTheFolderCount() throws IOException {
        Path folder = tempDir.resolve("Project");
        Path file = project(folder, "Game.vfproj", VALID);
        project(folder, "Game.vfproj.bak", VALID);
        project(folder, "readme.txt", "x");
        project(folder.resolve(".vertexforge"), "Hidden.vfproj", VALID);
        project(folder.resolve("Old"), "Old.vfproj", VALID);
        Files.createDirectories(folder.resolve("Folder.vfproj"));

        assertEquals(TestSupport.real(file), found(folder).file());
    }

    @Test
    void aFolderWithoutAProjectFileIsRefused() throws IOException {
        Path folder = tempDir.resolve("None");
        project(folder, "readme.txt", "x");
        project(folder.resolve("Sub"), "Sub.vfproj", VALID);

        PickedProject.Refused refused = refused(folder);

        assertEquals("Contains no .vfproj file", refused.reason());
        assertEquals(folder, refused.picked());
    }

    @Test
    void aFolderWithSeveralProjectFilesAsksWhichOne() throws IOException {
        Path folder = tempDir.resolve("Two");
        Path lower = project(folder, "b.vfproj", VALID);
        Path upper = project(folder, "A.VFPROJ", VALID);
        Path broken = project(folder, "c.vfproj", "{ broken");

        PickedProject result = inspector.inspect(folder);

        PickedProject.Choose choose = assertInstanceOf(PickedProject.Choose.class, result, result::toString);
        assertEquals(folder, choose.folder());
        assertEquals(folder, choose.picked());
        List<PickedProject> candidates = choose.candidates();
        assertEquals(3, candidates.size(), "sorted by name without regard to letter case: A, b, c");
        assertEquals(TestSupport.real(upper), assertInstanceOf(PickedProject.Found.class, candidates.get(0)).file());
        assertEquals(TestSupport.real(lower), assertInstanceOf(PickedProject.Found.class, candidates.get(1)).file());
        PickedProject.Refused refused = assertInstanceOf(PickedProject.Refused.class, candidates.get(2));
        assertEquals(broken, refused.picked());
        assertTrue(refused.reason().startsWith("Not valid JSON at line 1, column "), refused.reason());
    }

    @Test
    void aFolderNamedLikeAProjectFileIsAFolder() throws IOException {
        Path folder = tempDir.resolve("Game.vfproj");
        Path file = project(folder, "Game.vfproj", VALID);

        assertEquals(TestSupport.real(file), found(folder).file());
    }

    @Test
    void anUnfinishedCreationIsRefused() throws IOException {
        Path staging = tempDir.resolve(".Game" + ProjectCreator.STAGING_INFIX + "00000000-0000-0000-0000-000000001685");
        project(staging, "Game.vfproj", VALID);

        assertEquals("Is a project the launcher had not finished creating", refused(staging).reason());
    }

    @Test
    @EnabledOnOs(OS.WINDOWS)
    void anotherLetterCaseGivesTheSameRealPath() throws IOException {
        Path file = project(tempDir.resolve("Game"), "Game.vfproj", VALID);
        Path shouted = Path.of(file.toString().toUpperCase(Locale.ROOT));

        // Path.equals ignores letter case on Windows, so compare the text.
        assertEquals(TestSupport.real(file).toString(), found(shouted).file().toString());
    }

    @Test
    void aDotDotSegmentGivesTheSameRealPath() throws IOException {
        Path file = project(tempDir.resolve("Game"), "Game.vfproj", VALID);
        Path roundabout = tempDir.resolve("Game").resolve("..").resolve("Game").resolve("Game.vfproj");

        assertEquals(TestSupport.real(file).toString(), found(roundabout).file().toString());
    }

    @Test
    void anEightDotThreeSpellingGivesTheSameRealPath() throws IOException {
        // The CI runner's temp folder is spelled with a short name (RUNNER~1); most machines have no such spelling.
        assumeTrue(!tempDir.toString().equals(TestSupport.real(tempDir).toString()),
                "the temp folder has no other spelling here");
        Path file = project(tempDir.resolve("Game"), "Game.vfproj", VALID);

        assertEquals(TestSupport.real(file).toString(), found(file).file().toString());
    }

    @Test
    void aLinkToAProjectFileGivesTheRealFile() throws IOException {
        Path target = project(tempDir.resolve("Real"), "Game.vfproj", VALID);
        Path link = tempDir.resolve("Linked.vfproj");
        try {
            Files.createSymbolicLink(link, target);
        } catch (IOException | UnsupportedOperationException | SecurityException e) {
            assumeTrue(false, "this machine cannot create symbolic links: " + e);
        }

        assertEquals(TestSupport.real(target), found(link).file());
    }

    @Test
    void nothingIsWritten() throws IOException {
        Path folder = tempDir.resolve("Two");
        project(folder, "A.vfproj", VALID);
        project(folder, "B.vfproj", "not json");
        List<String> before = TestSupport.fileNames(folder).stream().sorted().toList();

        inspector.inspect(folder);
        inspector.inspect(folder.resolve("A.vfproj"));

        assertEquals(before, TestSupport.fileNames(folder).stream().sorted().toList());
    }

    @Test
    void aNullPathIsAProgrammingError() {
        assertThrows(NullPointerException.class, () -> inspector.inspect(null));
    }
}
