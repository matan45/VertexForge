package com.vertexforge.launcher.project;

import java.io.IOException;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;

/**
 * A stand-in for an editor that tries to open a project (VK-1691), for {@code EditorLockProbeHoldTest}: it asks for
 * the EXCLUSIVE lock on the byte at {@link EditorLockProbe#LOCK_OFFSET} of an existing lock file, as
 * {@code launch::ProjectLock} does, once, without waiting. With the second argument {@code launcher} it asks for the
 * byte a launcher's hold takes ({@link EditorLockProbe#LAUNCHER_OFFSET}) instead. A second process is needed: Java file
 * locks belong to the whole JVM.
 * <p>
 * Arguments: the lock file (it must exist; it is never created), and optionally {@code launcher}. Prints {@link #HELD}
 * when another process holds the byte, {@link #FREE} when the exclusive lock was taken (it is released at once); exits
 * 0, or 1 on any failure.
 */
public final class LockAttemptMain {

    public static final String HELD = "HELD";
    public static final String FREE = "FREE";

    private LockAttemptMain() {
    }

    public static void main(String[] args) {
        long offset = args.length > 1 && args[1].equals("launcher")
                ? EditorLockProbe.LAUNCHER_OFFSET
                : EditorLockProbe.LOCK_OFFSET;
        try (FileChannel channel = FileChannel.open(Path.of(args[0]), StandardOpenOption.READ,
                StandardOpenOption.WRITE)) {
            FileLock lock = channel.tryLock(offset, 1, false);
            if (lock == null) {
                System.out.println(HELD);
            } else {
                lock.release();
                System.out.println(FREE);
            }
            System.out.flush();
            System.exit(0);
        } catch (IOException | RuntimeException e) {
            e.printStackTrace();
            System.exit(1);
        }
    }
}
