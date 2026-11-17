package com.vertexforge.launcher.instance;

import com.vertexforge.launcher.instance.InstanceMessages.Ack;
import com.vertexforge.launcher.instance.InstanceMessages.InvalidMessageException;

import java.io.IOException;
import java.nio.channels.Channel;
import java.nio.channels.ClosedChannelException;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.channels.OverlappingFileLockException;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.charset.CharacterCodingException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.util.Objects;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.ScheduledThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Consumer;

/**
 * One launcher per user (VK-1685): a second launcher hands its command line to the one already running, which comes
 * to the front, and exits.
 * <p>
 * Two files in the launcher's settings folder: {@code <base>.lock}, whose file lock marks the running launcher (the
 * OS releases it when that process ends, however it ends; the file itself is never deleted), and {@code <base>.sock},
 * the Unix-domain socket it listens on. The base is {@code launcher} for the packaged launcher and
 * {@code launcher-dev} for a jar or {@code javafx:run}, so a development run never talks to the installed one.
 * <ol>
 *   <li>The launcher that takes the lock is the running one ({@link Primary}). Only it ever deletes the socket file:
 *       a stale one left by a launcher that crashed blocks the bind, and a launcher without the lock deleting it
 *       could remove the new running launcher's socket, leaving two launchers that each think they are the first.</li>
 *   <li>A launcher that finds the lock taken connects, reads the greeting, passes on its right to take the
 *       foreground, sends its request and waits for the answer ({@link Forwarded}). While that fails (the running
 *       launcher is still starting, is closing, or has just ended) it tries the lock and the socket again until its
 *       deadline, then gives up and opens its own window ({@link Independent}), so a double-click is never lost.</li>
 *   <li>Without Unix-domain sockets (before Windows 10 1803, or a socket path that is too long) the running launcher
 *       keeps only the lock; a second launcher then opens its own window when its deadline passes.</li>
 * </ol>
 * The lock is a whole-file {@link FileLock}: on Windows it belongs to the handle, so another process's
 * {@code tryLock} returns {@code null}, and another claim in this JVM gets {@link OverlappingFileLockException}
 * (both mean "held"). Every blocking read and write is bounded by a watchdog that closes the channel.
 */
public final class SingleInstance {

    public static final String PACKAGED_NAME = "launcher";
    public static final String DEV_NAME = "launcher-dev";
    public static final String LOCK_EXTENSION = ".lock";
    public static final String SOCKET_EXTENSION = ".sock";
    /** The error a closing launcher answers with. */
    static final String CLOSING = "closing";

    private static final long PID = ProcessHandle.current().pid();

    private SingleInstance() {
        // Utility class - prevent instantiation
    }

    /** {@link #PACKAGED_NAME} for the packaged launcher, {@link #DEV_NAME} otherwise. */
    public static String baseName(boolean packaged) {
        return packaged ? PACKAGED_NAME : DEV_NAME;
    }

    public static Path lockFile(Path configDir, String baseName) {
        return configDir.resolve(baseName + LOCK_EXTENSION);
    }

    public static Path socketFile(Path configDir, String baseName) {
        return configDir.resolve(baseName + SOCKET_EXTENSION);
    }

    /**
     * @param sockets       the Unix-domain sockets
     * @param grant         passes the foreground right to the running launcher
     * @param warn          where problems are reported; called from the claiming thread and, for bad requests, from
     *                      the running launcher's connection threads
     * @param readTimeout   how long the running launcher waits for a connected launcher's request
     * @param retryInterval the pause between two attempts of a launcher that finds the lock taken
     */
    public record Options(LocalSockets sockets, ForegroundGrant grant, Consumer<String> warn,
                          Duration readTimeout, Duration retryInterval) {

        public static final Duration READ_TIMEOUT = Duration.ofSeconds(1);
        public static final Duration RETRY_INTERVAL = Duration.ofMillis(25);

        public Options {
            Objects.requireNonNull(sockets, "sockets");
            Objects.requireNonNull(grant, "grant");
            Objects.requireNonNull(warn, "warn");
            requirePositive(readTimeout, "readTimeout");
            requirePositive(retryInterval, "retryInterval");
        }

        /** The real sockets and foreground grant, with the default timings. */
        public static Options system(Consumer<String> warn) {
            return new Options(LocalSockets.system(), ForegroundGrant.system(), warn, READ_TIMEOUT, RETRY_INTERVAL);
        }

        private static void requirePositive(Duration duration, String name) {
            Objects.requireNonNull(duration, name);
            if (duration.isNegative() || duration.isZero()) {
                throw new IllegalArgumentException(name + " must be positive");
            }
        }
    }

    /** How a launcher start ended up. */
    public sealed interface Claim permits Primary, Forwarded, Independent {
    }

    /** The request was handed to the running launcher: this launcher exits. */
    public record Forwarded() implements Claim {
    }

    /**
     * No running launcher took the request in time, or the lock could not be used: this launcher opens its own
     * window, without the lock, as launchers did before VK-1685. The reason has already been passed to
     * {@link Options#warn}.
     */
    public record Independent(String reason) implements Claim {
        public Independent {
            Objects.requireNonNull(reason, "reason");
        }
    }

    /**
     * Claims the single instance for a launcher start whose command line is {@code message}. Blocks for at most a
     * moment past {@code deadlineNanos}; never throws for anything the file system or the other launcher does.
     *
     * @param deadlineNanos the {@link System#nanoTime()} value after which a launcher that finds the lock taken gives
     *                      up and runs {@link Independent}
     */
    public static Claim claim(Path configDir, String baseName, InstanceMessage message, long deadlineNanos,
                              Options options) {
        Objects.requireNonNull(configDir, "configDir");
        Objects.requireNonNull(baseName, "baseName");
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(options, "options");

        try {
            Files.createDirectories(configDir);
        } catch (IOException | RuntimeException e) {
            return independent(options, "the folder " + configDir + " could not be created: " + describe(e));
        }
        Path lockFile = lockFile(configDir, baseName);
        Path socketFile = socketFile(configDir, baseName);

        String lastFailure = "no launcher answered";
        while (true) {
            HeldLock held;
            try {
                held = tryLock(lockFile);
            } catch (IOException | RuntimeException e) {
                return independent(options, "the lock file " + lockFile + " could not be used: " + describe(e));
            }
            if (held != null) {
                return becomePrimary(held, socketFile, message, options);
            }
            if (System.nanoTime() - deadlineNanos >= 0) {
                break;
            }

            String failure = forward(socketFile, message, deadlineNanos, options);
            if (failure == null) {
                return new Forwarded();
            }
            lastFailure = failure;

            long remaining = deadlineNanos - System.nanoTime();
            if (remaining <= 0) {
                // One last look at the lock: the running launcher may have just ended.
                continue;
            }
            try {
                Thread.sleep(Duration.ofNanos(Math.min(options.retryInterval().toNanos(), remaining)));
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                lastFailure = "the wait was interrupted";
                break;
            }
        }
        return independent(options, "the launcher that is already running did not take the request in time ("
                + lastFailure + ")");
    }

    private static Independent independent(Options options, String reason) {
        options.warn().accept("Single instance: " + reason + "; this launcher opens its own window");
        return new Independent(reason);
    }

    /** The lock, or {@code null} when another launcher (in this process or another) holds it. */
    private static HeldLock tryLock(Path lockFile) throws IOException {
        FileChannel channel = FileChannel.open(lockFile, StandardOpenOption.CREATE, StandardOpenOption.WRITE);
        boolean keep = false;
        try {
            FileLock lock;
            try {
                lock = channel.tryLock();
            } catch (OverlappingFileLockException e) {
                // Another claim in this JVM holds it.
                lock = null;
            }
            if (lock == null) {
                return null;
            }
            keep = true;
            return new HeldLock(channel, lock);
        } finally {
            if (!keep) {
                // On Windows a lock belongs to its own handle, so closing this one never releases another's.
                closeQuietly(channel);
            }
        }
    }

    private static Primary becomePrimary(HeldLock held, Path socketFile, InstanceMessage message, Options options) {
        InstanceInbox inbox = new InstanceInbox();
        inbox.offer(message);

        ServerSocketChannel server = null;
        String problem = null;
        try {
            // Only the holder of the lock may delete it (see the class comment).
            Files.deleteIfExists(socketFile);
            server = options.sockets().bind(socketFile);
        } catch (Exception | LinkageError e) {
            problem = describe(e);
        }

        Primary primary = new Primary(held, socketFile, server, inbox, options);
        if (server == null) {
            options.warn().accept("Single instance: other launchers cannot hand their projects to this one ("
                    + problem + "); a second launcher will open its own window");
        } else {
            primary.startAccepting();
        }
        return primary;
    }

    /**
     * One attempt to hand {@code message} to the running launcher.
     *
     * @return {@code null} when it took the request, else why not
     */
    private static String forward(Path socketFile, InstanceMessage message, long deadlineNanos, Options options) {
        SocketChannel channel;
        try {
            channel = options.sockets().connect(socketFile);
        } catch (Exception | LinkageError e) {
            return "could not connect: " + describe(e);
        }
        AtomicBoolean timedOut = new AtomicBoolean();
        ScheduledFuture<?> watchdog = Watchdog.closeAfter(channel, deadlineNanos - System.nanoTime(), timedOut);
        try (channel) {
            long pid = InstanceMessages.parseGreeting(LineIo.readLine(channel, InstanceMessages.MAX_LINE_BYTES));
            // Before the request: the running launcher comes to the front as soon as it has it.
            options.grant().allow(pid);
            LineIo.writeLine(channel, InstanceMessages.request(message));
            Ack ack = InstanceMessages.parseAck(LineIo.readLine(channel, InstanceMessages.MAX_LINE_BYTES));
            if (ack.ok()) {
                return null;
            }
            return "it refused the request: " + (ack.error() != null ? ack.error() : "no reason given");
        } catch (InvalidMessageException e) {
            return "it answered something unexpected: " + e.getMessage();
        } catch (Exception | LinkageError e) {
            return timedOut.get() ? "it did not answer in time" : "the connection failed: " + describe(e);
        } finally {
            watchdog.cancel(false);
        }
    }

    static String describe(Throwable error) {
        String message = error.getMessage();
        String type = error.getClass().getSimpleName();
        return message == null || message.isBlank() ? type : type + ": " + message;
    }

    private static void closeQuietly(Channel channel) {
        if (channel == null) {
            return;
        }
        try {
            channel.close();
        } catch (IOException | RuntimeException ignored) {
            // Nothing more can be done.
        }
    }

    /** The lock file's channel and its lock. */
    private record HeldLock(FileChannel channel, FileLock lock) {
        void release() {
            try {
                lock.release();
            } catch (IOException | RuntimeException ignored) {
                // Closing the channel below releases it anyway.
            } finally {
                closeQuietly(channel);
            }
        }
    }

    /** Closes channels whose time is up; one daemon thread for every exchange of this process. */
    private static final class Watchdog {
        private static final ScheduledThreadPoolExecutor EXECUTOR = create();

        private static ScheduledThreadPoolExecutor create() {
            ScheduledThreadPoolExecutor executor = new ScheduledThreadPoolExecutor(1, runnable -> {
                Thread thread = new Thread(runnable, "vf-instance-watchdog");
                thread.setDaemon(true);
                return thread;
            });
            executor.setRemoveOnCancelPolicy(true);
            return executor;
        }

        /** Closes {@code channel} after {@code delayNanos} (at once when it is not positive) and sets {@code fired}. */
        static ScheduledFuture<?> closeAfter(Channel channel, long delayNanos, AtomicBoolean fired) {
            return EXECUTOR.schedule(() -> {
                fired.set(true);
                closeQuietly(channel);
            }, Math.max(0, delayNanos), TimeUnit.NANOSECONDS);
        }
    }

    /**
     * This launcher is the running one. It holds the lock until {@link #release}, and, when {@link #listening}, takes
     * other launchers' requests into its {@link #inbox} on its own daemon threads ({@code vf-instance-accept}, and
     * one {@code vf-instance-conn-N} per connection).
     */
    public static final class Primary implements Claim, AutoCloseable {
        private final HeldLock held;
        private final Path socketFile;
        private final ServerSocketChannel server;
        private final InstanceInbox inbox;
        private final Options options;
        private final AtomicInteger connections = new AtomicInteger();
        private final AtomicBoolean released = new AtomicBoolean();
        private volatile boolean accepting;

        private Primary(HeldLock held, Path socketFile, ServerSocketChannel server, InstanceInbox inbox,
                        Options options) {
            this.held = held;
            this.socketFile = socketFile;
            this.server = server;
            this.inbox = inbox;
            this.options = options;
        }

        /** The requests for this launcher's window; it already holds this launcher's own command line. */
        public InstanceInbox inbox() {
            return inbox;
        }

        /** Whether other launchers can reach this one; {@code false} in lock-only mode and after {@link #release}. */
        public boolean listening() {
            return accepting && !released.get();
        }

        /**
         * Stops taking requests, then gives up the single instance: closes the server, deletes the socket file (the
         * lock is still held) and releases the lock. A launcher waiting meanwhile then takes over. Idempotent.
         */
        public void release() {
            if (!released.compareAndSet(false, true)) {
                return;
            }
            inbox.markClosing();
            if (server != null) {
                closeQuietly(server);
                try {
                    Files.deleteIfExists(socketFile);
                } catch (IOException | RuntimeException e) {
                    options.warn().accept("Single instance: could not delete " + socketFile + ": " + describe(e));
                }
            }
            held.release();
        }

        @Override
        public void close() {
            release();
        }

        private void startAccepting() {
            Thread acceptor = new Thread(this::acceptLoop, "vf-instance-accept");
            acceptor.setDaemon(true);
            accepting = true;
            try {
                acceptor.start();
            } catch (RuntimeException | OutOfMemoryError e) {
                accepting = false;
                closeQuietly(server);
                options.warn().accept("Single instance: could not start listening (" + describe(e)
                        + "); a second launcher will open its own window");
            }
        }

        private void acceptLoop() {
            while (server.isOpen()) {
                SocketChannel client;
                try {
                    client = server.accept();
                } catch (ClosedChannelException e) {
                    // release() closed the server.
                    return;
                } catch (Throwable t) {
                    if (!server.isOpen()) {
                        return;
                    }
                    // Never spin on a failing accept.
                    if (!pause(options.retryInterval())) {
                        return;
                    }
                    continue;
                }
                if (client == null) {
                    continue;
                }
                try {
                    Thread handler = new Thread(() -> serve(client),
                            "vf-instance-conn-" + connections.incrementAndGet());
                    handler.setDaemon(true);
                    handler.start();
                } catch (Throwable t) {
                    closeQuietly(client);
                }
            }
        }

        /** One connection: greeting, one request line, one answer. Nothing it does can stop the accept loop. */
        private void serve(SocketChannel client) {
            AtomicBoolean timedOut = new AtomicBoolean();
            ScheduledFuture<?> watchdog = null;
            try (client) {
                watchdog = Watchdog.closeAfter(client, options.readTimeout().toNanos(), timedOut);
                LineIo.writeLine(client, InstanceMessages.greeting(PID));
                String answer;
                try {
                    InstanceMessage message = InstanceMessages.parseRequest(
                            LineIo.readLine(client, InstanceMessages.MAX_LINE_BYTES));
                    answer = inbox.offer(message) ? InstanceMessages.ack() : InstanceMessages.refusal(CLOSING);
                } catch (InvalidMessageException e) {
                    options.warn().accept("Single instance: ignored a request from another launcher: "
                            + e.getMessage());
                    answer = InstanceMessages.refusal(e.getMessage());
                } catch (LineIo.LineTooLongException | CharacterCodingException e) {
                    options.warn().accept("Single instance: ignored a request from another launcher: "
                            + describe(e));
                    answer = InstanceMessages.refusal(describe(e));
                }
                LineIo.writeLine(client, answer);
            } catch (Throwable t) {
                // The other side went away, stayed silent past the read timeout or was cut off: nothing to answer.
            } finally {
                if (watchdog != null) {
                    watchdog.cancel(false);
                }
            }
        }

        /** Sleeps; {@code false} when interrupted. */
        private static boolean pause(Duration duration) {
            try {
                Thread.sleep(duration);
                return true;
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return false;
            }
        }
    }
}
