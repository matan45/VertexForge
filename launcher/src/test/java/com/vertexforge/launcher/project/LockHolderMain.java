package com.vertexforge.launcher.project;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;

/**
 * A stand-in for an editor that has a project open, for {@code EditorLockProbeChildProcessTest} (VK-1686). It does
 * what {@code launch::ProjectLock} does (VK-1682): creates {@code .vertexforge/editor.lock}, takes an exclusive lock on
 * the byte at {@link EditorLockProbe#LOCK_OFFSET}, then writes {@code {"pid":N}\n}. A second process is needed: Java
 * file locks belong to the whole JVM, so a lock taken by the test's own process would not stand for another editor.
 * <p>
 * Argument: the lock file. Prints {@code LOCKED <pid>} once it holds the lock, then holds it until its standard input
 * closes; exits 0, or 1 on any failure (with the stack trace).
 */
public final class LockHolderMain {

    public static final String LOCKED = "LOCKED";

    private LockHolderMain() {
    }

    public static void main(String[] args) {
        try {
            Path lockFile = Path.of(args[0]);
            Files.createDirectories(lockFile.getParent());
            try (FileChannel channel = FileChannel.open(lockFile, StandardOpenOption.CREATE, StandardOpenOption.READ,
                    StandardOpenOption.WRITE);
                 FileLock ignored = channel.lock(EditorLockProbe.LOCK_OFFSET, 1, false)) {
                long pid = ProcessHandle.current().pid();
                channel.truncate(0);
                channel.write(ByteBuffer.wrap(("{\"pid\":" + pid + "}\n").getBytes(StandardCharsets.US_ASCII)), 0);
                channel.force(true);
                System.out.println(LOCKED + " " + pid);
                System.out.flush();
                // Holds the lock until the test closes this process's standard input.
                while (System.in.read() >= 0) {
                    // Keeps reading until end of stream.
                }
            }
            System.exit(0);
        } catch (IOException | RuntimeException e) {
            e.printStackTrace();
            System.exit(1);
        }
    }
}
