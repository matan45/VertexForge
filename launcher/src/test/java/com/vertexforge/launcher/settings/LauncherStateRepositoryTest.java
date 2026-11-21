package com.vertexforge.launcher.settings;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.TestSupport.CapturedLog;
import com.vertexforge.launcher.settings.LauncherState.SavedWindow;
import com.vertexforge.launcher.settings.LauncherState.SortKey;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;
import org.junit.jupiter.api.io.TempDir;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import java.io.IOException;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.TimeUnit;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** VK-1694: what the launcher remembers by itself, in a file of its own. */
class LauncherStateRepositoryTest {

    @TempDir
    Path tempDir;

    private Path file;
    private CapturedLog log;

    @BeforeEach
    void setUp() {
        file = tempDir.resolve("config").resolve(LauncherStateRepository.FILE_NAME);
        log = TestSupport.log(tempDir.resolve("logs"));
    }

    private LauncherStateRepository repository() {
        return new LauncherStateRepository(file, TestSupport.fixedClock(), log.log);
    }

    private void write(String content) throws IOException {
        Files.createDirectories(file.getParent());
        Files.writeString(file, content);
    }

    @Test
    void anAbsentFileRemembersNothingAndCreatesNothing() {
        assertEquals(LauncherState.EMPTY, repository().load());
        assertFalse(Files.exists(file.getParent()));
        assertEquals("", log.console());
    }

    @Test
    void theWindowIsRememberedExactly() {
        SavedWindow window = new SavedWindow(-1920.5, 37.25, 1024.75, 700.0, true);

        assertTrue(repository().saveWindow(window));

        assertEquals(Optional.of(window), repository().load().window());
    }

    @Test
    void theSortIsRememberedInOrderAndAnEmptySortRemovesIt() throws IOException {
        List<SortKey> sort = List.of(new SortKey("name", true), new SortKey("lastOpened", false));

        assertTrue(repository().saveProjectSort(sort));
        assertEquals(sort, repository().load().projectSort());

        assertTrue(repository().saveProjectSort(List.of()));
        assertEquals(List.of(), repository().load().projectSort());
        assertFalse(Files.readString(file).contains(LauncherStateRepository.PROJECT_SORT));
    }

    @Test
    void eachSaveKeepsTheOtherMembers() throws IOException {
        write("{\"version\": 1, \"future\": 5}");
        SavedWindow window = new SavedWindow(10, 20, 900, 650, false);
        List<SortKey> sort = List.of(new SortKey("status", true));

        repository().saveWindow(window);
        repository().saveProjectSort(sort);

        LauncherState state = repository().load();
        assertEquals(Optional.of(window), state.window());
        assertEquals(sort, state.projectSort());
        JsonObject root = JsonParser.parseString(Files.readString(file)).getAsJsonObject();
        assertEquals(5, root.get("future").getAsInt());
    }

    @ParameterizedTest
    @ValueSource(strings = {
            "{\"x\": 1, \"y\": 2, \"width\": 0, \"height\": 600}",
            "{\"x\": 1, \"y\": 2, \"width\": -800, \"height\": 600}",
            "{\"x\": 1, \"y\": 2, \"width\": 800}",
            "{\"x\": \"1\", \"y\": 2, \"width\": 800, \"height\": 600}",
            "{\"x\": 1, \"y\": 2, \"width\": 800, \"height\": 600, \"maximized\": \"yes\"}",
            "{\"x\": 1e999, \"y\": 2, \"width\": 800, \"height\": 600}",
            "[1, 2, 800, 600]",
            "true"})
    void aWindowThatIsNotValidIsIgnoredWithAWarning(String window) throws IOException {
        write("{\"version\": 1, \"window\": " + window + "}");

        assertEquals(Optional.empty(), repository().load().window());
        assertTrue(log.console().contains(LauncherStateRepository.WINDOW), log.console());
    }

    @Test
    void aWindowWithoutMaximizedIsNotMaximized() throws IOException {
        write("{\"version\": 1, \"window\": {\"x\": 1, \"y\": 2, \"width\": 800, \"height\": 600}}");

        assertEquals(Optional.of(new SavedWindow(1, 2, 800, 600, false)), repository().load().window());
    }

    @Test
    void sortKeysThatAreNotValidAreSkippedWithAWarning() throws IOException {
        write("{\"version\": 1, \"projectSort\": [{\"column\": \"name\", \"ascending\": true}, 5,"
                + " {\"column\": 3, \"ascending\": true}, {\"column\": \"path\", \"ascending\": \"no\"},"
                + " {\"column\": \"path\", \"ascending\": false}]}");

        assertEquals(List.of(new SortKey("name", true), new SortKey("path", false)),
                repository().load().projectSort());
        assertTrue(log.console().contains("sort key"), log.console());
    }

    @Test
    void aSortThatIsNotAListIsIgnored() throws IOException {
        write("{\"version\": 1, \"projectSort\": \"name\"}");

        assertEquals(List.of(), repository().load().projectSort());
        assertTrue(log.console().contains(LauncherStateRepository.PROJECT_SORT), log.console());
    }

    @Test
    void loadNeverMovesACorruptFile() throws IOException {
        write("{oops");
        byte[] original = Files.readAllBytes(file);

        assertEquals(LauncherState.EMPTY, repository().load());

        assertArrayEquals(original, Files.readAllBytes(file));
        assertEquals(Set.of(LauncherStateRepository.FILE_NAME), TestSupport.fileNames(file.getParent()));
    }

    @Test
    void aSaveSetsACorruptFileAsideAndStartsAgain() throws IOException {
        write("{oops");
        SavedWindow window = new SavedWindow(0, 0, 800, 600, false);

        assertTrue(repository().saveWindow(window));

        assertEquals(Optional.of(window), repository().load().window());
        assertTrue(log.console().contains("was damaged"), log.console());
    }

    @Test
    void aNewerFileIsReadButNeverWritten() throws IOException {
        write("{\"version\": 7, \"window\": {\"x\": 1, \"y\": 2, \"width\": 800, \"height\": 600}}");
        byte[] before = Files.readAllBytes(file);

        assertEquals(Optional.of(new SavedWindow(1, 2, 800, 600, false)), repository().load().window());
        assertFalse(repository().saveWindow(new SavedWindow(5, 5, 900, 700, false)));

        assertArrayEquals(before, Files.readAllBytes(file));
        assertTrue(log.console().contains("Could not remember the window position"), log.console());
    }

    /** A save never holds up the store thread for long: it gives up after about a second, with a warning. */
    @Test
    @Timeout(value = 30, unit = TimeUnit.SECONDS)
    void aHeldLockMakesASaveGiveUpWithAWarning() throws IOException {
        Files.createDirectories(file.getParent());
        try (FileChannel channel = FileChannel.open(file.resolveSibling(LauncherStateRepository.FILE_NAME + ".lock"),
                StandardOpenOption.CREATE, StandardOpenOption.WRITE);
             FileLock ignored = channel.lock()) {
            long start = System.nanoTime();

            assertFalse(repository().saveProjectSort(List.of(new SortKey("name", true))));

            long millis = TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - start);
            assertTrue(millis < 5_000, "gave up after " + millis + " ms");
        }
        assertTrue(log.console().contains("Could not remember the project list's sort"), log.console());
    }

    @Test
    void aSavedWindowMustHaveFiniteBoundsAndASize() {
        assertThrows(IllegalArgumentException.class, () -> new SavedWindow(Double.NaN, 0, 800, 600, false));
        assertThrows(IllegalArgumentException.class, () -> new SavedWindow(0, 0, Double.POSITIVE_INFINITY, 600,
                false));
        assertThrows(IllegalArgumentException.class, () -> new SavedWindow(0, 0, 800, 0, false));
        assertTrue(SavedWindow.valid(-5, -5, 1, 1));
        assertFalse(SavedWindow.valid(0, 0, -1, 600));
    }
}
