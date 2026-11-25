package com.vertexforge.launcher.session;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class LaunchStatusReaderTest {

    @TempDir
    Path tempDir;

    private Path write(String line) throws IOException {
        return Files.writeString(tempDir.resolve("status.json"), line, StandardCharsets.UTF_8);
    }

    /** The exact line the editor writes (LaunchStatusFile.cpp formatLaunchStatus), with its trailing newline. */
    @Test
    void readsEachStateTheEditorWrites() throws IOException {
        assertEquals(Optional.of(new LaunchStatus(LaunchStatus.State.LOADING, 0,
                        "Loading project C:\\Games\\Demo\\Demo.vfproj", 1234)),
                LaunchStatusReader.read(write("{\"state\":\"loading\",\"code\":0,\"message\":"
                        + "\"Loading project C:\\\\Games\\\\Demo\\\\Demo.vfproj\",\"pid\":1234}\n")));
        assertEquals(Optional.of(new LaunchStatus(LaunchStatus.State.READY, 0, "Ready", 7)),
                LaunchStatusReader.read(write("{\"state\":\"ready\",\"code\":0,\"message\":\"Ready\",\"pid\":7}\n")));
        assertEquals(Optional.of(new LaunchStatus(LaunchStatus.State.FAILED, 3, "cannot open project", 7)),
                LaunchStatusReader.read(write(
                        "{\"state\":\"failed\",\"code\":3,\"message\":\"cannot open project\",\"pid\":7}\n")));
    }

    @Test
    void aCrashHasANegativeExceptionCode() throws IOException {
        LaunchStatus crash = LaunchStatusReader.read(write("{\"state\":\"failed\",\"code\":-1073741819,"
                + "\"message\":\"Editor crashed: ACCESS_VIOLATION\",\"pid\":7}")).orElseThrow();

        assertEquals(0xC0000005, crash.code());
        assertTrue(crash.isCrash());
        assertFalse(new LaunchStatus(LaunchStatus.State.FAILED, 3, "", 7).isCrash());
        assertFalse(new LaunchStatus(LaunchStatus.State.READY, -1, "", 7).isCrash());
    }

    @Test
    void anotherProcessesStatusIsIgnored() throws IOException {
        Path file = write("{\"state\":\"ready\",\"code\":0,\"message\":\"\",\"pid\":7}");

        assertTrue(LaunchStatusReader.read(file, 7).isPresent());
        assertEquals(Optional.empty(), LaunchStatusReader.read(file, 8));
    }

    @Test
    void anythingElseIsNoNews() throws IOException {
        assertEquals(Optional.empty(), LaunchStatusReader.read(tempDir.resolve("missing.json")));
        assertEquals(Optional.empty(), LaunchStatusReader.read(write("")));
        assertEquals(Optional.empty(), LaunchStatusReader.read(write("{\"state\":\"ready\",\"code\":0,")));
        assertEquals(Optional.empty(), LaunchStatusReader.read(write(
                "{\"state\":\"starting\",\"code\":0,\"message\":\"\",\"pid\":7}")));
        assertEquals(Optional.empty(), LaunchStatusReader.read(write(
                "{\"state\":\"ready\",\"code\":\"0\",\"message\":\"\",\"pid\":7}")));
        assertEquals(Optional.empty(), LaunchStatusReader.read(write(
                "{\"state\":\"ready\",\"code\":0,\"message\":\"\"}")));
        assertEquals(Optional.empty(), LaunchStatusReader.read(write(
                "{\"state\":\"ready\",\"code\":0.5,\"message\":\"\",\"pid\":7}")));
        assertEquals(Optional.empty(), LaunchStatusReader.read(write("[1,2]")));
        assertEquals(Optional.empty(), LaunchStatusReader.read(write(
                "{\"state\":\"ready\",\"code\":0,\"message\":\"\",\"pid\":7} trailing")));
    }

    @Test
    void unknownMembersAreIgnored() throws IOException {
        assertEquals(Optional.of(new LaunchStatus(LaunchStatus.State.READY, 0, "ok", 7)),
                LaunchStatusReader.read(write(
                        "{\"state\":\"ready\",\"code\":0,\"message\":\"ok\",\"pid\":7,\"future\":true}")));
    }

    /** VK-1732: the exact shape the editor writes (LaunchStatusFile.cpp formatLaunchStatus), the folder after pid. */
    @Test
    void readsTheCrashFolder() throws IOException {
        Path folder = Path.of("C:\\Users\\me\\.vertexforge\\editor\\crashes");

        LaunchStatus status = LaunchStatusReader.read(write("{\"state\":\"loading\",\"code\":0,\"message\":\"Loading\","
                + "\"pid\":1234,\"crashDir\":\"C:\\\\Users\\\\me\\\\.vertexforge\\\\editor\\\\crashes\"}\n"))
                .orElseThrow();

        assertEquals(new LaunchStatus(LaunchStatus.State.LOADING, 0, "Loading", 1234, Optional.of(folder)), status);
    }

    @Test
    void aLineWithoutACrashFolderNamesNone() throws IOException {
        LaunchStatus status = LaunchStatusReader.read(write(
                "{\"state\":\"ready\",\"code\":0,\"message\":\"ok\",\"pid\":7}")).orElseThrow();

        assertEquals(Optional.empty(), status.crashDir());
    }

    /** The other four members decide whether a line is readable; a bad folder only falls back. */
    private void assertCrashFolderIgnored(String crashDirJson) throws IOException {
        Optional<LaunchStatus> status = LaunchStatusReader.read(write(
                "{\"state\":\"ready\",\"code\":0,\"message\":\"ok\",\"pid\":7,\"crashDir\":" + crashDirJson + "}"));

        assertEquals(Optional.of(new LaunchStatus(LaunchStatus.State.READY, 0, "ok", 7, Optional.empty())), status,
                crashDirJson);
    }

    @Test
    void aRelativeCrashFolderIsIgnoredButTheLineIsKept() throws IOException {
        assertCrashFolderIgnored("\"crashes\"");
        assertCrashFolderIgnored("\"\"");
        assertCrashFolderIgnored("\"\\\\Users\\\\me\\\\crashes\"");
        assertCrashFolderIgnored("\"C:crashes\"");
    }

    @Test
    void aCrashFolderThatIsNotAStringIsIgnoredButTheLineIsKept() throws IOException {
        assertCrashFolderIgnored("42");
        assertCrashFolderIgnored("null");
        assertCrashFolderIgnored("true");
        assertCrashFolderIgnored("[\"C:\\\\crashes\"]");
        assertCrashFolderIgnored("{\"path\":\"C:\\\\crashes\"}");
    }

    @Test
    void aCrashFolderThatIsNoPathIsIgnoredButTheLineIsKept() throws IOException {
        assertCrashFolderIgnored("\"C:\\\\Users\\\\me\\\\cra\\u0000shes\"");
    }

    @Test
    void invalidUtf8IsNoNews() throws IOException {
        Path file = tempDir.resolve("status.json");
        Files.write(file, new byte[]{'{', (byte) 0xFF, '}'});

        assertEquals(Optional.empty(), LaunchStatusReader.read(file));
    }
}
