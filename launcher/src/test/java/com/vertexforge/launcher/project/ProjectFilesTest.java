package com.vertexforge.launcher.project;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ProjectFilesTest {

    @ParameterizedTest
    @ValueSource(strings = {"Game.vfproj", "Game.VFPROJ", "Game.VfProj", "My.Game.vfproj", "x.vfproj", "Tom & Jerry's Game.vfproj"})
    void theExtensionMatchesInAnyLetterCase(String fileName) {
        assertTrue(ProjectFiles.isProjectFileName(fileName), fileName);
    }

    @ParameterizedTest
    @ValueSource(strings = {".vfproj", ".VFPROJ", "Game.vfproj.bak", "Game.vfprojx", "Game.txt", "Game", "vfproj", ""})
    void otherNamesAreNotProjectFiles(String fileName) {
        assertFalse(ProjectFiles.isProjectFileName(fileName), fileName);
    }

    @Test
    void nullIsNotAProjectFile() {
        assertFalse(ProjectFiles.isProjectFileName(null));
        assertFalse(ProjectFiles.isProjectFile(null));
    }

    @Test
    void aPathIsJudgedByItsFileNameOnly() {
        assertTrue(ProjectFiles.isProjectFile(Path.of("does-not-exist", "Game.VFPROJ")));
        assertFalse(ProjectFiles.isProjectFile(Path.of("Game.vfproj", "readme.txt")));
        assertFalse(ProjectFiles.isProjectFile(Path.of("").toAbsolutePath().getRoot()), "a root has no file name");
    }

    @Test
    void theStemDropsTheExtensionInAnyLetterCase() {
        assertEquals("Game", ProjectFiles.stem("Game.vfproj"));
        assertEquals("Game", ProjectFiles.stem("Game.VFPROJ"));
        assertEquals("My.Game", ProjectFiles.stem("My.Game.VfProj"));
    }

    @Test
    void theStemOfAnyOtherNameIsTheNameItself() {
        assertEquals("Game.txt", ProjectFiles.stem("Game.txt"));
        assertEquals(".vfproj", ProjectFiles.stem(".vfproj"));
        assertNull(ProjectFiles.stem(null));
    }
}
