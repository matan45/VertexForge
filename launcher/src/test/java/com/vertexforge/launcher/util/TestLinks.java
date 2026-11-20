package com.vertexforge.launcher.util;

import com.vertexforge.launcher.TestSupport;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.charset.Charset;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.attribute.BasicFileAttributes;

import static org.junit.jupiter.api.Assumptions.assumeTrue;

/**
 * Symbolic links and junctions for the file-tree tests (VK-1690). A test that cannot make one is skipped, not failed.
 * <p>
 * JUnit's temp-folder cleanup walks INTO a junction (Java sees it as a folder), so every test that makes one must
 * remove it again itself, with {@link #removeLink}, before it ends.
 */
public final class TestLinks {

    private TestLinks() {
    }

    /** A symbolic link at {@code link} to {@code target}; skips the test when this machine cannot make one. */
    public static Path symlink(Path link, Path target) {
        try {
            return Files.createSymbolicLink(link, target);
        } catch (IOException | UnsupportedOperationException | SecurityException e) {
            assumeTrue(false, "this machine cannot create symbolic links: " + e);
            throw new IllegalStateException(e);
        }
    }

    /**
     * A junction at {@code link} to the folder {@code target}, made with {@code mklink /J} (no privilege needed);
     * skips the test on other systems or when it cannot be made.
     */
    public static Path junction(Path link, Path target) {
        assumeTrue(TestSupport.isWindows(), "junctions are a Windows thing");
        try {
            Process process = new ProcessBuilder("cmd", "/c", "mklink", "/J", link.toString(), target.toString())
                    .redirectErrorStream(true)
                    .start();
            String output = new String(process.getInputStream().readAllBytes(), Charset.defaultCharset());
            int code = process.waitFor();
            boolean made = code == 0 && Files.readAttributes(link, BasicFileAttributes.class,
                    LinkOption.NOFOLLOW_LINKS).isOther();
            assumeTrue(made, "could not create a junction (exit " + code + "): " + output);
            return link;
        } catch (IOException e) {
            assumeTrue(false, "could not create a junction: " + e);
            throw new UncheckedIOException(e);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException(e);
        }
    }

    /** Removes a link or junction itself, never what it points to; nothing happens when it is gone already. */
    public static void removeLink(Path link) {
        try {
            Files.deleteIfExists(link);
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }
}
