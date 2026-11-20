package com.vertexforge.launcher.project;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.channels.OverlappingFileLockException;
import java.nio.charset.StandardCharsets;
import java.nio.file.AccessDeniedException;
import java.nio.file.FileAlreadyExistsException;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.util.Objects;
import java.util.OptionalLong;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.locks.ReentrantLock;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Tells whether an editor has a project folder open (VK-1686), with the probe the editor's lock protocol was built for
 * ({@code VFEngine/utilities/launch/ProjectLock.hpp}, VK-1682):
 * <ul>
 *   <li>The editor holds an exclusive lock on one byte at {@link #LOCK_OFFSET} of
 *       {@code <project folder>/.vertexforge/editor.lock}, far beyond the file's content ({@code {"pid":N}}).</li>
 *   <li>This probe opens that file for reading only, so it never creates the folder or the file and never writes,
 *       and asks for a SHARED lock on the same byte. Getting it means no editor holds the folder; it is released at
 *       once. Not getting it means an editor holds it.</li>
 *   <li>The editor retries its own lock for about 250 ms, so the instant this probe holds the shared lock never makes
 *       a starting editor give up. After an editor is killed, the system may drop its lock a little later, so a held
 *       lock is tried again a few times before it counts.</li>
 * </ul>
 * A probe blocks (a network share can take seconds), so it only runs on {@code ProjectHealthService}'s lock-check
 * threads, with a timeout of its own. It never throws.
 */
public final class EditorLockProbe {

    /** The editor's state folder inside a project folder ({@code EditorStateDirectory.hpp}). */
    public static final String STATE_FOLDER = ".vertexforge";
    /** The lock file inside {@link #STATE_FOLDER}. */
    public static final String LOCK_FILE = "editor.lock";
    /** The byte the editor locks ({@code launch::editorLockOffset}). */
    public static final long LOCK_OFFSET = 1L << 32;

    public static final int DEFAULT_RETRIES = 3;
    public static final Duration DEFAULT_RETRY_DELAY = Duration.ofMillis(250);

    /** The lock file holds a few bytes; a pid is read from at most this many. */
    static final int PID_BYTES = 64;
    private static final Pattern PID = Pattern.compile("\"pid\"\\s*:\\s*(\\d{1,18})");

    /**
     * Java file locks belong to the whole JVM: a second {@code tryLock} on a file this process already locks throws
     * {@link OverlappingFileLockException}. Two rows of one folder are checked at the same time, so their probes queue
     * here (as the store's do, {@code store/FileLocks}); each holds it only while it holds the shared lock.
     */
    private static final ConcurrentHashMap<Path, ReentrantLock> LOCAL_LOCKS = new ConcurrentHashMap<>();

    /** Waits between attempts; a seam for tests. */
    @FunctionalInterface
    public interface Sleeper {
        void sleep(Duration duration) throws InterruptedException;
    }

    private final int retries;
    private final Duration retryDelay;
    private final Sleeper sleeper;

    /**
     * @param retries    how many more times a held lock is tried before it counts as held
     * @param retryDelay the wait before each retry
     */
    public EditorLockProbe(int retries, Duration retryDelay, Sleeper sleeper) {
        if (retries < 0) {
            throw new IllegalArgumentException("retries must not be negative");
        }
        this.retries = retries;
        this.retryDelay = Objects.requireNonNull(retryDelay, "retryDelay");
        this.sleeper = Objects.requireNonNull(sleeper, "sleeper");
    }

    /** {@link #DEFAULT_RETRIES} retries, {@link #DEFAULT_RETRY_DELAY} apart: about a second for a held lock. */
    public static EditorLockProbe system() {
        return new EditorLockProbe(DEFAULT_RETRIES, DEFAULT_RETRY_DELAY, duration -> Thread.sleep(duration));
    }

    /** {@code <projectFolder>/.vertexforge/editor.lock}. */
    public static Path lockFile(Path projectFolder) {
        return projectFolder.resolve(STATE_FOLDER).resolve(LOCK_FILE);
    }

    /** Whether an editor holds {@code projectFolder} (the folder of a {@code .vfproj}). Blocks; never throws. */
    public EditorLockState probe(Path projectFolder) {
        Objects.requireNonNull(projectFolder, "projectFolder");
        Path lock = lockFile(projectFolder);
        // READ only: the file is opened only when it exists (never created), and nothing can be written through it.
        try (FileChannel channel = FileChannel.open(lock, StandardOpenOption.READ)) {
            ReentrantLock local = LOCAL_LOCKS.computeIfAbsent(lock.toAbsolutePath().normalize(),
                    key -> new ReentrantLock());
            for (int attempt = 0; ; attempt++) {
                Attempt result = attempt(channel, local);
                if (result == Attempt.FREE) {
                    return EditorLockState.FREE;
                }
                if (attempt >= retries) {
                    return result == Attempt.HELD
                            ? new EditorLockState.Held(pid(channel))
                            // This launcher itself was checking the same file under another spelling of its path.
                            : new EditorLockState.Unknown("it was being checked under another path at the same time");
                }
                sleeper.sleep(retryDelay);
            }
        } catch (NoSuchFileException e) {
            // No state folder or no lock file: no editor has ever locked this folder, or it never could.
            return EditorLockState.FREE;
        } catch (AccessDeniedException e) {
            return new EditorLockState.Unknown(PathFailures.ACCESS_DENIED);
        } catch (IOException e) {
            return new EditorLockState.Unknown(PathFailures.reasonOf(e));
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return new EditorLockState.Unknown("the check was interrupted");
        } catch (RuntimeException e) {
            // UnsupportedOperationException (no lock support), or anything unexpected: the editor decides on open.
            return new EditorLockState.Unknown(e.toString());
        }
    }

    private enum Attempt {
        FREE,
        HELD,
        OVERLAP
    }

    /** One try for the shared lock, released at once; nothing else happens while it is held. */
    private static Attempt attempt(FileChannel channel, ReentrantLock local) throws IOException, InterruptedException {
        local.lockInterruptibly();
        try {
            FileLock shared;
            try {
                shared = channel.tryLock(LOCK_OFFSET, 1, true);
            } catch (OverlappingFileLockException e) {
                return Attempt.OVERLAP;
            }
            if (shared == null) {
                return Attempt.HELD;
            }
            shared.release();
            return Attempt.FREE;
        } finally {
            local.unlock();
        }
    }

    /** The byte after the editor's: a launcher that holds the folder ({@link #hold}) locks it EXCLUSIVELY. */
    public static final long LAUNCHER_OFFSET = LOCK_OFFSET + 1;

    /** Why {@link #hold} fails while another launcher holds the folder. */
    public static final String HELD_BY_LAUNCHER = "another VertexForge launcher is changing this project right now";

    /**
     * The lock held for a while (VK-1691: a script-library update). While it is held:
     * <ul>
     *   <li>an editor cannot open the folder: its exclusive lock on the editor's byte fails against this shared one
     *       (it then exits with "already open");</li>
     *   <li>another launcher cannot hold the folder too: this one also holds the next byte ({@link #LAUNCHER_OFFSET})
     *       exclusively, which neither the editor nor a probe ever touches.</li>
     * </ul>
     * Taken and released on ONE thread, the job's, because it also holds this JVM's queue for the file (other probes
     * of the folder wait meanwhile).
     */
    public static final class Hold implements AutoCloseable {
        private final FileChannel channel;
        private final FileLock editorByte;
        private final FileLock launcherByte;
        private final ReentrantLock local;
        private boolean closed;

        private Hold(FileChannel channel, FileLock editorByte, FileLock launcherByte, ReentrantLock local) {
            this.channel = channel;
            this.editorByte = editorByte;
            this.launcherByte = launcherByte;
            this.local = local;
        }

        /** Lets go of the locks. Only on the thread that took them; a second call does nothing. */
        @Override
        public void close() {
            if (closed) {
                return;
            }
            closed = true;
            release(launcherByte);
            release(editorByte);
            try {
                channel.close();
            } catch (IOException e) {
                // Nothing more can be done; the system drops the locks with the handle.
            }
            local.unlock();
        }

        private static void release(FileLock lock) {
            try {
                lock.release();
            } catch (IOException | RuntimeException e) {
                // Closing the channel releases it too.
            }
        }
    }

    /** What {@link #hold} got. */
    public sealed interface HoldResult {
        /** The lock is held until {@link Hold#close}. */
        record Holding(Hold hold) implements HoldResult {
            public Holding {
                Objects.requireNonNull(hold, "hold");
            }
        }

        /**
         * The lock could not be taken: an editor holds it ({@link EditorLockState.Held}), another launcher does, or it
         * could not be checked ({@link EditorLockState.Unknown}).
         */
        record NotHeld(EditorLockState state) implements HoldResult {
            public NotHeld {
                Objects.requireNonNull(state, "state");
            }
        }
    }

    /**
     * Takes the shared lock on the editor's byte of {@code projectFolder} and the exclusive lock on the launcher's
     * ({@link Hold}), and keeps them; the editor's byte is retried as {@link #probe} retries it. Unlike a probe, it
     * opens the lock file for writing (an exclusive lock needs it) and creates the state folder (one level) and an
     * empty lock file when there is none: a lock needs a file, and it is the file the editor itself would create.
     * Nothing is ever written into the file. Blocks; never throws.
     */
    public HoldResult hold(Path projectFolder) {
        Objects.requireNonNull(projectFolder, "projectFolder");
        Path lockPath = lockFile(projectFolder);
        FileChannel channel;
        try {
            channel = openForHold(lockPath);
        } catch (AccessDeniedException e) {
            return new HoldResult.NotHeld(new EditorLockState.Unknown(PathFailures.ACCESS_DENIED));
        } catch (IOException | RuntimeException e) {
            return new HoldResult.NotHeld(new EditorLockState.Unknown(e instanceof IOException io
                    ? PathFailures.reasonOf(io)
                    : e.toString()));
        }

        ReentrantLock local = LOCAL_LOCKS.computeIfAbsent(lockPath.toAbsolutePath().normalize(),
                key -> new ReentrantLock());
        boolean holding = false;
        try {
            local.lockInterruptibly();
            try {
                FileLock editorByte = null;
                for (int attempt = 0; editorByte == null; attempt++) {
                    try {
                        editorByte = channel.tryLock(LOCK_OFFSET, 1, true);
                    } catch (OverlappingFileLockException e) {
                        // This JVM holds the byte under another spelling of the path: wait as for a held lock.
                        editorByte = null;
                    }
                    if (editorByte == null) {
                        if (attempt >= retries) {
                            return new HoldResult.NotHeld(new EditorLockState.Held(pid(channel)));
                        }
                        sleeper.sleep(retryDelay);
                    }
                }
                FileLock launcherByte;
                try {
                    launcherByte = channel.tryLock(LAUNCHER_OFFSET, 1, false);
                } catch (OverlappingFileLockException e) {
                    launcherByte = null;
                }
                if (launcherByte == null) {
                    return new HoldResult.NotHeld(new EditorLockState.Unknown(HELD_BY_LAUNCHER));
                }
                holding = true;
                return new HoldResult.Holding(new Hold(channel, editorByte, launcherByte, local));
            } finally {
                if (!holding) {
                    local.unlock();
                }
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return new HoldResult.NotHeld(new EditorLockState.Unknown("the check was interrupted"));
        } catch (IOException e) {
            return new HoldResult.NotHeld(new EditorLockState.Unknown(PathFailures.reasonOf(e)));
        } catch (RuntimeException e) {
            return new HoldResult.NotHeld(new EditorLockState.Unknown(e.toString()));
        } finally {
            if (!holding) {
                // Closing the channel releases any lock already taken on it.
                try {
                    channel.close();
                } catch (IOException e) {
                    // Nothing more can be done.
                }
            }
        }
    }

    /**
     * Opens the lock file for reading and writing (never truncating), creating the state folder (one level) and an
     * empty file first when needed.
     */
    private static FileChannel openForHold(Path lockPath) throws IOException {
        try {
            return FileChannel.open(lockPath, StandardOpenOption.READ, StandardOpenOption.WRITE);
        } catch (NoSuchFileException e) {
            try {
                Files.createDirectory(lockPath.getParent());
            } catch (FileAlreadyExistsException exists) {
                // The state folder is there; only the file is missing.
            }
            return FileChannel.open(lockPath, StandardOpenOption.READ, StandardOpenOption.WRITE,
                    StandardOpenOption.CREATE);
        }
    }

    /** The {@code "pid"} the lock file names, read without moving the channel; empty when there is none. */
    private static OptionalLong pid(FileChannel channel) {
        try {
            ByteBuffer buffer = ByteBuffer.allocate(PID_BYTES);
            while (buffer.hasRemaining() && channel.read(buffer, buffer.position()) > 0) {
                // Reads until the buffer is full or the file ends.
            }
            buffer.flip();
            Matcher matcher = PID.matcher(StandardCharsets.US_ASCII.decode(buffer));
            return matcher.find() ? OptionalLong.of(Long.parseLong(matcher.group(1))) : OptionalLong.empty();
        } catch (IOException | RuntimeException e) {
            return OptionalLong.empty();
        }
    }
}
