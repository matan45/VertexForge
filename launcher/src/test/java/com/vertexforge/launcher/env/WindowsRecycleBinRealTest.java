package com.vertexforge.launcher.env;

import com.sun.nio.file.ExtendedOpenOption;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledIfSystemProperty;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;

import java.io.IOException;
import java.nio.channels.SeekableByteChannel;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.Comparator;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * A REAL recycle (VK-1695): it moves a small folder it creates under target/ into this machine's Recycle Bin, so it
 * runs only on request:
 * {@code mvnw -B test -Dtest=WindowsRecycleBinRealTest -Dvertexforge.realRecycleBin=true
 * -Dsurefire.failIfNoSpecifiedTests=false} (surefire hands Maven's user properties to the test JVM).
 * <p>
 * It proves the two things the dry run cannot: Windows moves the folder as ONE item (exactly one PreDeleteItem), and a
 * folder holding a file that is open without delete sharing is refused with nothing deleted.
 */
@EnabledOnOs(OS.WINDOWS)
@EnabledIfSystemProperty(named = "vertexforge.realRecycleBin", matches = "true")
class WindowsRecycleBinRealTest {

    /** Under target/, on the build machine's drive, which (for this test to mean anything) is a fixed drive. */
    private static Path newFolder() throws IOException {
        Path target = Path.of("target").toAbsolutePath().normalize();
        Path folder = Files.createDirectories(target.resolve("vf-recycle-test-" + UUID.randomUUID()));
        Files.writeString(folder.resolve("Game.vfproj"), "{\"projectName\":\"RecycleTest\"}");
        Files.createDirectories(folder.resolve("assets"));
        Files.writeString(folder.resolve("assets").resolve("note.txt"), "recycled by WindowsRecycleBinRealTest");
        return folder;
    }

    private static void deleteIfPresent(Path folder) throws IOException {
        if (!Files.exists(folder)) {
            return;
        }
        try (Stream<Path> walk = Files.walk(folder)) {
            for (Path path : walk.sorted(Comparator.reverseOrder()).toList()) {
                Files.deleteIfExists(path);
            }
        }
    }

    @Test
    void aFolderOnAFixedDriveGoesIntoTheRecycleBinAsOneItem() throws IOException {
        Path folder = newFolder();
        try {
            assertEquals(DriveTypes.Kind.LOCAL, new WindowsDriveTypes().kindOf(folder.getRoot()));

            WindowsRecycleBin.Attempt<RecycleResult> attempt = new WindowsRecycleBin().recycleAttempt(folder);

            String seen = attempt.answer() + " / "
                    + attempt.observation().map(ShellResults.Observation::describe).orElse("-");
            System.out.println("[WindowsRecycleBinRealTest] recycle: " + seen);
            assertEquals(new RecycleResult.Recycled(), attempt.answer(), seen);
            assertEquals(1, attempt.observation().orElseThrow().sink().preDeleteCalls(), seen);
            assertFalse(Files.exists(folder), "the folder is still there: " + seen);
        } finally {
            deleteIfPresent(folder);
        }
    }

    @Test
    void aFolderWithAFileInUseIsRefusedAndKeepsEveryFile() throws IOException {
        Path folder = newFolder();
        Path held = folder.resolve("assets").resolve("note.txt");
        try {
            try (SeekableByteChannel channel = Files.newByteChannel(held,
                    Set.of(StandardOpenOption.READ, ExtendedOpenOption.NOSHARE_DELETE))) {
                WindowsRecycleBin.Attempt<RecycleResult> attempt = new WindowsRecycleBin().recycleAttempt(folder);

                String seen = attempt.answer() + " / "
                        + attempt.observation().map(ShellResults.Observation::describe).orElse("-");
                System.out.println("[WindowsRecycleBinRealTest] in use: " + seen);
                assertEquals(new RecycleResult.Refused(ShellResults.IN_USE, true), attempt.answer(), seen);
                assertTrue(channel.isOpen());
                assertTrue(Files.isRegularFile(folder.resolve("Game.vfproj")), seen);
                assertTrue(Files.isRegularFile(held), seen);
                assertEquals("recycled by WindowsRecycleBinRealTest", Files.readString(held), seen);
            }
        } finally {
            deleteIfPresent(folder);
        }
    }
}
