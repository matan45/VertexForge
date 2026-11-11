package com.vertexforge.launcher.service;

import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.TreeMap;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The bundled templates/script-library.zip must be assets/scripts/lib byte for byte (VK-1676).
 * The zip packs the working tree, so the comparison is against the working tree, not git blobs.
 */
class ScriptLibraryResourceTest {

    private static Path scriptLibDir() {
        // Surefire passes the folder; the default matches Surefire's working directory (launcher/).
        return Path.of(System.getProperty("vertexforge.scriptLibDir", "../assets/scripts/lib"))
                .toAbsolutePath().normalize();
    }

    @Test
    void zipMatchesTheEngineScriptLibrary() throws IOException {
        Path libDir = scriptLibDir();
        assertTrue(Files.isDirectory(libDir),
                "assets/scripts/lib not found at " + libDir + ": the launcher build needs the full repo checkout");

        Map<String, Path> files = new TreeMap<>();
        try (Stream<Path> walk = Files.walk(libDir)) {
            walk.filter(Files::isRegularFile)
                    .forEach(file -> files.put(libDir.relativize(file).toString().replace('\\', '/'), file));
        }

        byte[] zip;
        try (InputStream in = getClass().getResourceAsStream("/templates/script-library.zip")) {
            assertNotNull(in, "templates/script-library.zip is missing: run the Maven build (pack-script-lib)");
            zip = in.readAllBytes();
        }
        Map<String, byte[]> entries = ProjectCreatorTest.zipEntries(zip);

        assertFalse(files.isEmpty());
        assertEquals(files.keySet(), entries.keySet());
        for (Map.Entry<String, Path> file : files.entrySet()) {
            assertArrayEquals(Files.readAllBytes(file.getValue()), entries.get(file.getKey()), file.getKey());
        }
    }
}
