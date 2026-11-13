package com.vertexforge.launcher.service;

import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertSame;

class ProjectTemplateTest {

    private static List<String> zipFiles() throws IOException {
        List<String> files = new ArrayList<>();
        try (InputStream in = ProjectTemplateTest.class.getResourceAsStream(ProjectCreator.LIBRARY_ZIP_RESOURCE)) {
            assertNotNull(in, "the launcher build packs the script library");
            try (ZipInputStream zip = new ZipInputStream(in)) {
                ZipEntry entry;
                while ((entry = zip.getNextEntry()) != null) {
                    if (!entry.isDirectory()) {
                        files.add(entry.getName());
                    }
                }
            }
        }
        return files;
    }

    @Test
    void theBundledTemplateIsTheBundledLibrary() throws IOException {
        List<String> files = zipFiles();
        ProjectTemplate template = ProjectTemplate.bundled();

        assertFalse(files.isEmpty());
        assertEquals(files, template.libraryFiles());
        // The project file, the scene, scripts.mtproj and Main.mt, then the library.
        assertEquals(4 + files.size(), template.fileCount());
        assertSame(template, ProjectTemplate.bundled(), "scanned once");
    }

    @Test
    void theDeepestFileIsTheLongestLibraryPath() throws IOException {
        int longest = zipFiles().stream().mapToInt(file -> ("scripts/lib/" + file).length()).max().orElseThrow();

        assertEquals(Math.max(longest, "scripts/scripts.mtproj".length()),
                ProjectTemplate.bundled().deepestRelativeLength("Game"));
    }

    @Test
    void aLongNameMakesItsProjectFileTheDeepest() {
        ProjectTemplate template = ProjectTemplate.of(List.of("core/A.mt"));
        String name = "n".repeat(60);

        assertEquals("scripts/scripts.mtproj".length(), template.deepestRelativeLength("Game"));
        assertEquals(name.length() + ".vfproj".length(), template.deepestRelativeLength(name));
    }
}
