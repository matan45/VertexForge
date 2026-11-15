package com.vertexforge.launcher.service;

import com.vertexforge.launcher.service.EditorLauncherTest.FakeProcess;
import com.vertexforge.launcher.service.EditorLauncherTest.RecordingStarter;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

class ShellOpenerTest {

    private static final Path FILE = Path.of("C:\\Users\\a b\\.vertexforge\\logs\\Game, 1.log");

    @Test
    void opensAFileOrFolderThroughExplorer() throws IOException {
        RecordingStarter starter = new RecordingStarter(FakeProcess.exited(1), null);

        new ShellOpener(starter).open(FILE);

        ProcessBuilder builder = starter.started.getFirst();
        assertEquals(List.of("explorer.exe", FILE.toString()), builder.command());
        assertEquals(ProcessBuilder.Redirect.DISCARD, builder.redirectOutput());
        assertEquals(ProcessBuilder.Redirect.DISCARD, builder.redirectError());
    }

    /** Verified on Windows 11: "/select," and the path as two arguments selects a file whose path has a comma. */
    @Test
    void selectsAFileInItsFolder() throws IOException {
        RecordingStarter starter = new RecordingStarter(FakeProcess.exited(1), null);

        new ShellOpener(starter).showInFolder(FILE);

        assertEquals(List.of("explorer.exe", "/select,", FILE.toString()), starter.started.getFirst().command());
    }

    @Test
    void aStartFailureIsThrown() {
        RecordingStarter starter = new RecordingStarter(null, new IOException("no explorer"));

        assertThrows(IOException.class, () -> new ShellOpener(starter).open(FILE));
    }
}
