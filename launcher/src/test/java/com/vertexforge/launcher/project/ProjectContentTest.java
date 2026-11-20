package com.vertexforge.launcher.project;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.project.ProjectContent.Scope;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Locale;
import java.util.Optional;
import java.util.function.Predicate;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

class ProjectContentTest {

    @TempDir
    Path tempDir;

    /** {@code <temp>/Projects/Game/Game.vfproj}; nothing is created: the rules are lexical. */
    private Path projectFile() {
        return tempDir.resolve("Projects").resolve("Game").resolve("Game.vfproj");
    }

    private Path folder() {
        return projectFile().getParent();
    }

    @Test
    void theProjectFolderItselfIsInside() {
        ProjectContent content = ProjectContent.of(projectFile(), ".");

        assertEquals(Scope.INSIDE, content.scope());
        assertEquals(projectFile(), content.projectFile());
        assertEquals(folder(), content.folder());
        assertEquals(folder(), content.workingDirectory());
        assertEquals(".", content.relativeWorkingDirectory());
        assertEquals(List.of(".vertexforge", "scripts/compiled"), content.exclusions());
        assertEquals(Optional.empty(), content.unavailableReason());
    }

    @Test
    void aRelativeFolderInsideIsInside() {
        ProjectContent content = ProjectContent.of(projectFile(), "assets");

        assertEquals(Scope.INSIDE, content.scope());
        assertEquals(folder().resolve("assets"), content.workingDirectory());
        assertEquals("assets", content.relativeWorkingDirectory());
        assertEquals(List.of(".vertexforge", "assets/scripts/compiled"), content.exclusions());
    }

    @Test
    void theWorkingDirectoryIsNormalizedAsTheEditorDoes() {
        ProjectContent content = ProjectContent.of(projectFile(), "assets/./../assets/sub/");

        assertEquals(Scope.INSIDE, content.scope());
        assertEquals("assets/sub", content.relativeWorkingDirectory());
        assertEquals(List.of(".vertexforge", "assets/sub/scripts/compiled"), content.exclusions());
    }

    @Test
    void aDotDotThatLeavesTheFolderIsOutside() {
        for (String workingDirectory : List.of("..", "../Shared", "assets/../../Game2", "../Game/../Other")) {
            ProjectContent content = ProjectContent.of(projectFile(), workingDirectory);

            assertEquals(Scope.OUTSIDE, content.scope(), workingDirectory);
            assertNull(content.relativeWorkingDirectory(), workingDirectory);
            assertEquals(List.of(".vertexforge"), content.exclusions(), workingDirectory);
        }
        ProjectContent shared = ProjectContent.of(projectFile(), "../Shared");
        assertEquals(tempDir.resolve("Projects").resolve("Shared"), shared.workingDirectory());
        assertEquals(Optional.of("Its working directory (" + shared.workingDirectory() + ") lies outside the project "
                + "folder, so a backup or a copy could not cover the project's content."), shared.unavailableReason());
    }

    @Test
    void aDotDotThatComesBackIsInside() {
        ProjectContent content = ProjectContent.of(projectFile(), "../Game/assets");

        assertEquals(Scope.INSIDE, content.scope());
        assertEquals("assets", content.relativeWorkingDirectory());
    }

    @Test
    void anAbsolutePathInsideIsInsideWhateverItsCase() {
        ProjectContent exact = ProjectContent.of(projectFile(), folder().resolve("assets").toString());
        assertEquals(Scope.INSIDE, exact.scope());
        assertEquals("assets", exact.relativeWorkingDirectory());

        assumeTrue(TestSupport.isWindows(), "only Windows ignores case in paths");
        String upper = folder().toString().toUpperCase(Locale.ROOT) + "\\Assets";
        ProjectContent shouted = ProjectContent.of(projectFile(), upper);
        assertEquals(Scope.INSIDE, shouted.scope());
        assertEquals("Assets", shouted.relativeWorkingDirectory());
        assertEquals(List.of(".vertexforge", "Assets/scripts/compiled"), shouted.exclusions());

        ProjectContent self = ProjectContent.of(projectFile(), folder().toString().toLowerCase(Locale.ROOT));
        assertEquals(Scope.INSIDE, self.scope());
        assertEquals(".", self.relativeWorkingDirectory());
    }

    @Test
    void anAbsolutePathElsewhereIsOutside() {
        ProjectContent sibling = ProjectContent.of(projectFile(), tempDir.resolve("Projects/GameAssets").toString());
        assertEquals(Scope.OUTSIDE, sibling.scope(), "a folder whose name merely starts like the project's");

        assumeTrue(TestSupport.isWindows(), "drive letters are a Windows thing");
        ProjectContent other = ProjectContent.of(projectFile(), otherDrive() + "\\Assets");
        assertEquals(Scope.OUTSIDE, other.scope());
        assertEquals(Optional.of("Its working directory (" + otherDrive() + "\\Assets) lies outside the project "
                + "folder, so a backup or a copy could not cover the project's content."), other.unavailableReason());
    }

    @Test
    void aDriveRelativePathOnAnotherDriveIsOutside() {
        assumeTrue(TestSupport.isWindows(), "drive-relative paths are a Windows thing");

        ProjectContent content = ProjectContent.of(projectFile(), otherDrive() + "assets");

        assertEquals(Scope.OUTSIDE, content.scope());
    }

    @Test
    void aProjectAtADriveRootHasNoParent() {
        Path root = tempDir.getRoot();

        ProjectContent content = ProjectContent.of(root.resolve("Game.vfproj"), ".");

        assertEquals(Scope.NO_PARENT, content.scope());
        assertEquals(root, content.folder());
        assertNull(content.workingDirectory());
        assertEquals(Optional.of("The project folder is a drive root, so there is no folder beside it to put a "
                + "backup or a copy in."), content.unavailableReason());
    }

    @Test
    void aMissingBlankOrInvalidWorkingDirectoryIsInvalid() {
        for (String workingDirectory : new String[]{null, "", "   ", "assets" + (char) 0 + "x"}) {
            ProjectContent content = ProjectContent.of(projectFile(), workingDirectory);

            assertEquals(Scope.INVALID, content.scope(), String.valueOf(workingDirectory));
            assertNull(content.workingDirectory());
            assertEquals(Optional.of("Its working directory is missing or is not a valid path, so a backup or a copy "
                    + "could not tell which files belong to the project."), content.unavailableReason());
        }
    }

    @Test
    void aRelativeProjectFileIsMadeAbsolute() {
        ProjectContent content = ProjectContent.of(Path.of("SomeGame", "SomeGame.vfproj"), "assets");

        assertTrue(content.projectFile().isAbsolute());
        assertEquals(Path.of("SomeGame").toAbsolutePath().normalize(), content.folder());
        assertEquals(Scope.INSIDE, content.scope());
    }

    @Test
    void theExclusionsIgnoreCaseAndCoverWhatLiesInThem() {
        Predicate<String> excluded = ProjectContent.of(projectFile(), "assets").excluded();

        for (String path : List.of(".vertexforge", ".VertexForge", ".vertexforge/editor.lock",
                ".vertexforge/saves/slot1.json", "assets/scripts/compiled", "Assets/Scripts/COMPILED",
                "assets/scripts/compiled/scripts.mtcLib")) {
            assertTrue(excluded.test(path), path);
        }
        for (String path : List.of(".vertexforge2", ".vertexforgeX/file", "assets", "assets/scripts",
                "assets/scripts/compiledOld", "assets/scripts/compiled.txt", "scripts/compiled",
                "x/.vertexforge", "Game.vfproj")) {
            assertFalse(excluded.test(path), path);
        }
    }

    @Test
    void theStateFolderIsTheEditorLocksFolder() {
        assertEquals(EditorLockProbe.STATE_FOLDER, ProjectContent.STATE_FOLDER);
    }

    /**
     * The exclusions are exactly what a new project's {@code .gitignore} lists (the engine's {@code empty} template,
     * whose working directory is {@code assets}), both ways: each entry is excluded, and nothing else is.
     */
    @Test
    void theExclusionsAreTheEmptyTemplatesGitignore() throws IOException {
        Path gitignore = Path.of(System.getProperty("vertexforge.repoRoot", "..")).toAbsolutePath().normalize()
                .resolve("assets/templates/empty/project/.gitignore");
        assertTrue(Files.isRegularFile(gitignore), gitignore + " is missing");
        List<String> entries = Files.readAllLines(gitignore, StandardCharsets.UTF_8).stream()
                .map(String::strip)
                .filter(line -> !line.isEmpty() && !line.startsWith("#"))
                .toList();
        for (String entry : entries) {
            assertTrue(entry.startsWith("/") && entry.endsWith("/"), "a rooted folder entry: " + entry);
        }

        List<String> folders = entries.stream().map(entry -> entry.substring(1, entry.length() - 1)).toList();

        assertEquals(List.of(".vertexforge", "assets/scripts/compiled"), folders);
        assertEquals(folders, ProjectContent.of(projectFile(), "assets").exclusions());
    }

    /** A drive letter other than the temp folder's, for paths that must be on another drive. */
    private String otherDrive() {
        String root = tempDir.getRoot().toString();
        char letter = root.length() >= 2 && root.charAt(1) == ':' ? Character.toUpperCase(root.charAt(0)) : 'C';
        return (letter == 'Q' ? "R" : "Q") + ":";
    }
}
