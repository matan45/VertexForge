package com.vertexforge.launcher.instance;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.instance.InstanceRequest.Action;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.file.Path;
import java.util.List;
import java.util.Locale;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

/** The launcher's command line (VK-1685). */
class CommandLineTest {

    @TempDir
    Path workingDirectory;

    private InstanceMessage parse(String... args) {
        return CommandLine.parse(args, workingDirectory).message();
    }

    private static InstanceRequest open(Path path) {
        return new InstanceRequest(Action.OPEN, path);
    }

    private static InstanceRequest add(Path path) {
        return new InstanceRequest(Action.ADD, path);
    }

    @Test
    void noArgumentsAskForNothing() {
        assertEquals(InstanceMessage.EMPTY, parse());
        assertEquals(InstanceMessage.EMPTY, CommandLine.parse(null, workingDirectory).message());
        assertTrue(InstanceMessage.EMPTY.isEmpty());
    }

    @Test
    void aBarePathOpensTheProject() {
        Path project = workingDirectory.resolve("Game").resolve("Game.vfproj");

        assertEquals(List.of(open(project)), parse(project.toString()).requests());
    }

    @Test
    void relativePathsAreResolvedAgainstTheWorkingDirectoryAndNormalized() {
        InstanceMessage message = parse("Game.vfproj", "--open", "sub/../Other", "--add", "./third/Third.vfproj");

        assertEquals(List.of(
                open(workingDirectory.resolve("Game.vfproj")),
                open(workingDirectory.resolve("Other")),
                add(workingDirectory.resolve("third").resolve("Third.vfproj"))), message.requests());
        assertEquals(List.of(), message.problems());
    }

    @Test
    void theOrderIsKeptAndEveryOpenStaysAnOpen() {
        Path a = workingDirectory.resolve("a.vfproj");
        Path b = workingDirectory.resolve("b.vfproj");
        Path c = workingDirectory.resolve("c.vfproj");

        assertEquals(List.of(open(a), add(b), open(c)),
                parse(a.toString(), "--add", b.toString(), "--open", c.toString()).requests());
    }

    @Test
    void aMissingPathIsAProblemAndTheNextOptionIsNotSwallowed() {
        Path project = workingDirectory.resolve("x.vfproj");

        InstanceMessage message = parse("--add", "--open", project.toString(), "--open");

        assertEquals(List.of(open(project)), message.requests());
        assertEquals(List.of("--add needs a path", "--open needs a path"), message.problems());
    }

    @Test
    void anUnknownOptionIsAProblemAndTheOtherArgumentsAreStillUsed() {
        Path project = workingDirectory.resolve("a.vfproj");

        InstanceMessage message = parse("--weird", project.toString(), "--open=" + project);

        assertEquals(List.of(open(project)), message.requests());
        assertEquals(List.of("Unknown option --weird", "Unknown option --open=" + project), message.problems());
    }

    @Test
    void anInvalidOrEmptyPathIsAProblem() {
        InstanceMessage message = parse("bad\u0000path", "--add", " ");

        assertEquals(List.of(), message.requests());
        assertEquals(2, message.problems().size(), message.problems().toString());
        assertTrue(message.problems().get(0).startsWith("\"bad\u0000path\" is not a valid path"),
                message.problems().get(0));
        assertEquals("An empty path was given", message.problems().get(1));
    }

    @Test
    void windowsOnlyInvalidAndDriveRelativePathsAreProblems() {
        assumeTrue(TestSupport.isWindows());
        // A drive-relative path on another drive than the working directory's names no folder.
        String root = workingDirectory.getRoot().toString().toUpperCase(Locale.ROOT);
        String otherDrive = root.startsWith("Q") ? "R:game" : "Q:game";

        InstanceMessage message = parse("C:\\a<b", otherDrive);

        assertEquals(List.of(), message.requests());
        assertEquals(2, message.problems().size(), message.problems().toString());
        assertTrue(message.problems().get(0).startsWith("\"C:\\a<b\" is not a valid path"), message.problems().get(0));
        assertEquals("\"" + otherDrive + "\" is not a full path", message.problems().get(1));
    }

    @Test
    void theWorkingDirectoryMustBeAbsolute() {
        assertThrows(IllegalArgumentException.class, () -> CommandLine.parse(new String[0], Path.of("relative")));
    }

    @Test
    void aRequestPathMustBeAbsoluteAndIsStoredNormalized() {
        assertThrows(IllegalArgumentException.class, () -> new InstanceRequest(Action.OPEN, Path.of("relative")));
        assertEquals(workingDirectory.resolve("b"),
                new InstanceRequest(Action.ADD, workingDirectory.resolve("a").resolve("..").resolve("b")).path());
    }
}
