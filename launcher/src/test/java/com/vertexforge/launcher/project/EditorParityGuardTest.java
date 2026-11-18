package com.vertexforge.launcher.project;

import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.stream.Collectors;

import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The launcher's twins of the editor's rules (VK-1686) must follow the editor's source: the members it requires, its
 * messages, how it reads a schema version, and its lock protocol. A change in the engine fails here until the twin is
 * changed too. The engine source is read from this checkout (the launcher is built inside the repository).
 */
class EditorParityGuardTest {

    private static Path engineSource(String relative) {
        // Surefire's working directory is launcher/.
        Path file = Path.of("..", "VFEngine").resolve(relative).toAbsolutePath().normalize();
        assertTrue(Files.isRegularFile(file), file + " not found: the launcher build needs the full repo checkout");
        return file;
    }

    private static String read(String relative) throws IOException {
        return Files.readString(engineSource(relative), StandardCharsets.UTF_8);
    }

    private static void assertContains(String source, String expected, String what) {
        assertTrue(source.contains(expected), what + ": the engine no longer says " + expected);
    }

    private static String cppList(List<String> fields) {
        return fields.stream().map(field -> "\"" + field + "\"").collect(Collectors.joining(", ", "{", "}"));
    }

    @Test
    void theMemberRulesAndTheirWordsAreTheEditors() throws IOException {
        String serialization = read("utilities/serialization/ProjectSerialization.cpp");

        assertContains(serialization, "for (const char* field : " + cppList(EditorProjectRules.REQUIRED_STRINGS) + ")",
                "the members that must be strings");
        assertContains(serialization, "for (const char* field : " + cppList(EditorProjectRules.REQUIRED_NON_EMPTY)
                + ")", "the members that must not be empty");
        assertContains(serialization, "\"Invalid project file: missing or invalid '\") + field + \"' field\"",
                "the message for a missing member");
        assertContains(serialization, "\"Invalid project file: '\") + field + \"' cannot be empty\"",
                "the message for an empty member");
        assertContains(serialization, "\"Invalid schema version format: '\" + versionStr + \"' (expected MAJOR.MINOR)\"",
                "the message for an unreadable schema version");
    }

    @Test
    void theSchemaVersionIsReadAndComparedAsTheEditorDoes() throws IOException {
        String serialization = read("utilities/serialization/ProjectSerialization.cpp");

        assertContains(serialization, "versionStr.find('.')", "the split at the first dot");
        assertContains(serialization, "if (c < '0' || c > '9')", "ASCII digits only");
        assertContains(serialization, "out = UINT32_MAX;", "a part too large saturates");
        assertContains(serialization, "if (fileVersion.major > config::ProjectSchemaVersion::major)",
                "a newer schema MAJOR is refused (exit code 4)");
    }

    @Test
    void theLockProtocolIsTheOneTheProbeExpects() throws IOException {
        String header = read("utilities/launch/ProjectLock.hpp");
        String lock = read("utilities/launch/ProjectLock.cpp");
        String state = read("utilities/launch/EditorStateDirectory.hpp");

        assertTrue(EditorLockProbe.LOCK_OFFSET == 1L << 32, "the probe locks the byte at 2^32");
        assertContains(header, "editorLockOffset = 1ull << 32", "the byte the editor locks");
        assertContains(state, "editorStateDirName = L\"" + EditorLockProbe.STATE_FOLDER + "\"", "the state folder");
        assertContains(state, "editorLockFileName = L\"" + EditorLockProbe.LOCK_FILE + "\"", "the lock file");
        // The probe opens the file for reading while the editor holds it: the editor must share reading.
        assertContains(lock, "GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE",
                "the editor shares its lock file for reading");
        // The probe's shared lock lasts an instant: the editor must retry rather than give up at once.
        assertContains(lock, "constexpr int kLockRetries = 5;", "the editor retries its lock");
        assertContains(lock, "constexpr DWORD kLockRetryDelayMs = 50;", "the editor's retry delay");
    }
}
