package com.vertexforge.launcher.model;

import org.junit.jupiter.api.io.TempDir;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import java.nio.file.Path;
import java.util.UUID;

import static com.vertexforge.launcher.TestSupport.T0;
import static org.junit.jupiter.api.Assertions.assertEquals;

class RecentEntryTest {

    @TempDir
    Path tempDir;

    @ParameterizedTest
    @ValueSource(strings = {"Game.vfproj", "Game.VFPROJ", "Game.VfProj"})
    void withoutANameTheFileNameIsShownWithoutItsExtensionInAnyLetterCase(String fileName) {
        RecentEntry entry = RecentEntry.create(UUID.randomUUID(), tempDir.resolve("Game").resolve(fileName), null, T0);

        assertEquals("Game", entry.displayName());
    }

    @ParameterizedTest
    @ValueSource(strings = {"Game.txt", ".vfproj"})
    void anotherFileNameIsShownWhole(String fileName) {
        RecentEntry entry = RecentEntry.create(UUID.randomUUID(), tempDir.resolve(fileName), null, T0);

        assertEquals(fileName, entry.displayName());
    }
}
