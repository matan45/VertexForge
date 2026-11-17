package com.vertexforge.launcher.instance;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.instance.InstanceMessages.Ack;
import com.vertexforge.launcher.instance.InstanceRequest.Action;
import com.vertexforge.launcher.instance.SingleInstance.Claim;
import com.vertexforge.launcher.instance.SingleInstance.Forwarded;
import com.vertexforge.launcher.instance.SingleInstance.Independent;
import com.vertexforge.launcher.instance.SingleInstance.Options;
import com.vertexforge.launcher.instance.SingleInstance.Primary;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.io.InterruptedIOException;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * One launcher per user (VK-1685), over real Unix-domain sockets in a temp folder. The socket path must stay under
 * about 106 bytes, which the temp folder does. A socket file is a reparse point on Windows, so its existence is checked
 * without following links.
 */
@Timeout(value = 60, unit = TimeUnit.SECONDS)
class SingleInstanceTest {

    private static final String BASE = SingleInstance.DEV_NAME;

    @TempDir
    Path tempDir;

    private final List<String> warnings = new CopyOnWriteArrayList<>();
    private final List<Primary> primaries = new CopyOnWriteArrayList<>();

    @AfterEach
    void releaseEveryPrimary() {
        primaries.forEach(Primary::release);
    }

    private Path configDir() {
        return TestSupport.real(tempDir).resolve("config");
    }

    private Options options() {
        return options(LocalSockets.system(), ForegroundGrant.none(), Duration.ofSeconds(1));
    }

    private Options options(LocalSockets sockets) {
        return options(sockets, ForegroundGrant.none(), Duration.ofSeconds(1));
    }

    private Options options(LocalSockets sockets, ForegroundGrant grant, Duration readTimeout) {
        return new Options(sockets, grant, warnings::add, readTimeout, Duration.ofMillis(25));
    }

    private static long deadlineIn(long millis) {
        return System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(millis);
    }

    private static long millisSince(long startNanos) {
        return TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - startNanos);
    }

    private Primary primary(Path configDir, InstanceMessage message, Options options) {
        Claim claim = SingleInstance.claim(configDir, BASE, message, deadlineIn(2000), options);
        Primary primary = assertInstanceOf(Primary.class, claim, () -> "claim: " + claim + ", warnings " + warnings);
        primaries.add(primary);
        return primary;
    }

    private InstanceMessage open(String name) {
        return new InstanceMessage(List.of(new InstanceRequest(Action.OPEN, configDir().resolveSibling(name))),
                List.of());
    }

    private static BlockingQueue<InstanceMessage> attachQueue(Primary primary) {
        BlockingQueue<InstanceMessage> received = new LinkedBlockingQueue<>();
        primary.inbox().attach(received::add);
        return received;
    }

    private static LocalSockets unsupported() {
        return new LocalSockets() {
            @Override
            public ServerSocketChannel bind(Path socketFile) {
                throw new UnsupportedOperationException("Protocol family not supported");
            }

            @Override
            public SocketChannel connect(Path socketFile) {
                throw new UnsupportedOperationException("Protocol family not supported");
            }
        };
    }

    @Test
    void theFirstLauncherIsPrimaryAndCreatesTheSettingsFolder() throws Exception {
        Path dir = configDir().resolve("not").resolve("there");
        InstanceMessage own = open("Own.vfproj");

        Primary primary = primary(dir, own, options());

        assertTrue(primary.listening());
        assertTrue(Files.exists(SingleInstance.lockFile(dir, BASE)));
        assertTrue(Files.exists(SingleInstance.socketFile(dir, BASE), LinkOption.NOFOLLOW_LINKS));
        // The inbox already holds this launcher's own command line.
        assertEquals(own, attachQueue(primary).poll(5, TimeUnit.SECONDS));
        assertEquals(List.of(), warnings);
    }

    @Test
    void aSecondLauncherForwardsItsRequestAndIsDoneQuickly() throws Exception {
        Path dir = configDir();
        Primary primary = primary(dir, InstanceMessage.EMPTY, options());
        BlockingQueue<InstanceMessage> received = attachQueue(primary);
        assertEquals(InstanceMessage.EMPTY, received.poll(5, TimeUnit.SECONDS));
        InstanceMessage second = open("Second.vfproj");

        long start = System.nanoTime();
        Claim claim = SingleInstance.claim(dir, BASE, second, deadlineIn(1500), options());
        long elapsed = millisSince(start);

        assertInstanceOf(Forwarded.class, claim, () -> "warnings " + warnings);
        assertTrue(elapsed < 2000, elapsed + " ms");
        assertEquals(second, received.poll(5, TimeUnit.SECONDS));
    }

    @Test
    void aPlainFileWhereTheSocketGoesIsReplacedByTheNewPrimary() throws Exception {
        Path dir = configDir();
        Files.createDirectories(dir);
        Files.writeString(SingleInstance.socketFile(dir, BASE), "left over");

        Primary primary = primary(dir, InstanceMessage.EMPTY, options());

        assertTrue(primary.listening(), warnings::toString);
        assertInstanceOf(Forwarded.class,
                SingleInstance.claim(dir, BASE, open("A.vfproj"), deadlineIn(1500), options()));
    }

    @Test
    void aStaleSocketLeftByALauncherThatEndedIsReplaced() throws Exception {
        Path dir = configDir();
        Files.createDirectories(dir);
        // Bound and closed without deleting, like a launcher that crashed. Windows may or may not remove the file.
        LocalSockets.system().bind(SingleInstance.socketFile(dir, BASE)).close();

        Primary primary = primary(dir, InstanceMessage.EMPTY, options());

        assertTrue(primary.listening(), warnings::toString);
        assertInstanceOf(Forwarded.class,
                SingleInstance.claim(dir, BASE, open("A.vfproj"), deadlineIn(1500), options()));
    }

    @Test
    void aLauncherWithoutTheLockNeverDeletesTheSocketFileAndGivesUpAtItsDeadline() throws Exception {
        Path dir = configDir();
        Files.createDirectories(dir);
        Path marker = SingleInstance.socketFile(dir, BASE);
        Files.writeString(marker, "not a socket");

        try (FileChannel channel = FileChannel.open(SingleInstance.lockFile(dir, BASE), StandardOpenOption.CREATE,
                StandardOpenOption.WRITE);
             FileLock ignored = channel.lock()) {
            long start = System.nanoTime();
            Claim claim = SingleInstance.claim(dir, BASE, open("A.vfproj"), deadlineIn(300), options());
            long elapsed = millisSince(start);

            Independent independent = assertInstanceOf(Independent.class, claim);
            assertFalse(independent.reason().isBlank());
            assertTrue(elapsed >= 250 && elapsed < 10_000, elapsed + " ms");
        }
        assertEquals("not a socket", Files.readString(marker));
        assertTrue(warnings.stream().anyMatch(warning -> warning.contains("opens its own window")), warnings::toString);
    }

    @Test
    void aPrimaryThatBindsLateStillGetsTheRequest() throws Exception {
        Path dir = configDir();
        CountDownLatch binding = new CountDownLatch(1);
        LocalSockets slow = new LocalSockets() {
            @Override
            public ServerSocketChannel bind(Path socketFile) throws IOException {
                // The claim holds the lock by now.
                binding.countDown();
                try {
                    Thread.sleep(200);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    throw new InterruptedIOException();
                }
                return LocalSockets.system().bind(socketFile);
            }

            @Override
            public SocketChannel connect(Path socketFile) throws IOException {
                return LocalSockets.system().connect(socketFile);
            }
        };
        CompletableFuture<Claim> first = CompletableFuture.supplyAsync(
                () -> SingleInstance.claim(dir, BASE, InstanceMessage.EMPTY, deadlineIn(5000), options(slow)));
        assertTrue(binding.await(10, TimeUnit.SECONDS));
        InstanceMessage second = open("Late.vfproj");

        Claim claim = SingleInstance.claim(dir, BASE, second, deadlineIn(3000), options());

        Primary primary = assertInstanceOf(Primary.class, first.get(10, TimeUnit.SECONDS));
        primaries.add(primary);
        assertInstanceOf(Forwarded.class, claim, () -> "warnings " + warnings);
        BlockingQueue<InstanceMessage> received = attachQueue(primary);
        assertEquals(InstanceMessage.EMPTY, received.poll(5, TimeUnit.SECONDS));
        assertEquals(second, received.poll(5, TimeUnit.SECONDS));
    }

    @Test
    void whenTheRunningLauncherReleasesTheWaitingOneTakesOver() throws Exception {
        Path dir = configDir();
        Primary first = primary(dir, InstanceMessage.EMPTY, options(unsupported()));
        assertFalse(first.listening());
        InstanceMessage second = open("Next.vfproj");
        CompletableFuture<Claim> waiting = CompletableFuture.supplyAsync(
                () -> SingleInstance.claim(dir, BASE, second, deadlineIn(10_000), options()));

        Thread.sleep(200);
        assertFalse(waiting.isDone());
        first.release();

        Primary next = assertInstanceOf(Primary.class, waiting.get(15, TimeUnit.SECONDS));
        primaries.add(next);
        assertTrue(next.listening());
        assertEquals(second, attachQueue(next).poll(5, TimeUnit.SECONDS));
    }

    @Test
    void launchersStartingTogetherGiveExactlyOnePrimary() throws Exception {
        Path dir = configDir();
        int count = 8;
        CyclicBarrier start = new CyclicBarrier(count);
        ExecutorService threads = Executors.newFixedThreadPool(count);
        List<InstanceMessage> messages = new ArrayList<>();
        for (int i = 0; i < count; i++) {
            messages.add(open("Project" + i + ".vfproj"));
        }
        List<Claim> claims = new ArrayList<>();
        try {
            List<Future<Claim>> futures = new ArrayList<>();
            for (InstanceMessage message : messages) {
                futures.add(threads.submit(() -> {
                    start.await();
                    return SingleInstance.claim(dir, BASE, message, deadlineIn(10_000), options());
                }));
            }
            for (Future<Claim> future : futures) {
                claims.add(future.get(30, TimeUnit.SECONDS));
            }
        } finally {
            threads.shutdownNow();
        }

        List<Primary> found = claims.stream().filter(Primary.class::isInstance).map(Primary.class::cast).toList();
        primaries.addAll(found);
        assertEquals(1, found.size(), claims::toString);
        assertEquals(count - 1, claims.stream().filter(Forwarded.class::isInstance).count(), claims::toString);
        BlockingQueue<InstanceMessage> received = attachQueue(found.getFirst());
        Set<InstanceMessage> got = new HashSet<>();
        for (int i = 0; i < count; i++) {
            InstanceMessage message = received.poll(5, TimeUnit.SECONDS);
            assertNotNull(message, "only " + got.size() + " messages arrived");
            got.add(message);
        }
        assertEquals(Set.copyOf(messages), got);
    }

    @Test
    void withoutUnixSocketsThePrimaryKeepsOnlyTheLockAndASecondLauncherOpensItsOwnWindow() {
        Path dir = configDir();

        Primary primary = primary(dir, InstanceMessage.EMPTY, options(unsupported()));

        assertFalse(primary.listening());
        assertTrue(warnings.stream().anyMatch(warning -> warning.contains("Protocol family not supported")),
                warnings::toString);
        Claim second = SingleInstance.claim(dir, BASE, open("A.vfproj"), deadlineIn(300), options());
        assertFalse(assertInstanceOf(Independent.class, second).reason().isBlank());
    }

    @Test
    void aSocketPathThatIsTooLongGivesLockOnly() {
        Path dir = configDir().resolve("d".repeat(110));

        Primary primary = primary(dir, InstanceMessage.EMPTY, options());

        assertFalse(primary.listening());
        assertFalse(warnings.isEmpty());
    }

    @Test
    void theForegroundRightIsPassedToTheGreetingsPidBeforeTheRequestIsSent() throws Exception {
        Path dir = configDir();
        Primary primary = primary(dir, InstanceMessage.EMPTY, options());
        List<InstanceMessage> received = new CopyOnWriteArrayList<>();
        primary.inbox().attach(received::add);
        AtomicLong grantedPid = new AtomicLong(-1);
        AtomicInteger receivedAtGrant = new AtomicInteger(-1);
        ForegroundGrant grant = pid -> {
            grantedPid.set(pid);
            receivedAtGrant.set(received.size());
        };

        Claim claim = SingleInstance.claim(dir, BASE, open("A.vfproj"), deadlineIn(1500),
                options(LocalSockets.system(), grant, Duration.ofSeconds(1)));

        assertInstanceOf(Forwarded.class, claim);
        assertEquals(ProcessHandle.current().pid(), grantedPid.get());
        // Only the primary's own message had arrived when the grant was made.
        assertEquals(1, receivedAtGrant.get());
        assertEquals(2, received.size());
    }

    @Test
    void aClosingLauncherRefusesSoTheNewOneGivesUpAtItsDeadline() {
        Path dir = configDir();
        Primary primary = primary(dir, InstanceMessage.EMPTY, options());
        primary.inbox().markClosing();

        Claim claim = SingleInstance.claim(dir, BASE, open("A.vfproj"), deadlineIn(300), options());

        Independent independent = assertInstanceOf(Independent.class, claim);
        assertTrue(independent.reason().contains(SingleInstance.CLOSING), independent.reason());
    }

    @Test
    void aClosingLauncherThatThenReleasesHandsOverToTheWaitingOne() throws Exception {
        Path dir = configDir();
        Primary primary = primary(dir, InstanceMessage.EMPTY, options());
        primary.inbox().markClosing();
        CompletableFuture<Claim> waiting = CompletableFuture.supplyAsync(
                () -> SingleInstance.claim(dir, BASE, open("A.vfproj"), deadlineIn(10_000), options()));

        Thread.sleep(200);
        primary.release();

        Primary next = assertInstanceOf(Primary.class, waiting.get(15, TimeUnit.SECONDS));
        primaries.add(next);
        assertTrue(next.listening());
    }

    @Test
    void aRunningLauncherThatNeverAnswersIsCutOffAtTheDeadline() throws Exception {
        Path dir = configDir();
        Files.createDirectories(dir);
        Path socketFile = SingleInstance.socketFile(dir, BASE);
        List<SocketChannel> held = new CopyOnWriteArrayList<>();
        try (FileChannel channel = FileChannel.open(SingleInstance.lockFile(dir, BASE), StandardOpenOption.CREATE,
                StandardOpenOption.WRITE);
             FileLock ignored = channel.lock();
             ServerSocketChannel stuck = LocalSockets.system().bind(socketFile)) {
            // Accepts and then says nothing, like a launcher that hangs.
            Thread acceptor = new Thread(() -> {
                try {
                    while (true) {
                        held.add(stuck.accept());
                    }
                } catch (IOException e) {
                    // The test closed the server.
                }
            }, "stuck-acceptor");
            acceptor.setDaemon(true);
            acceptor.start();

            long start = System.nanoTime();
            Claim claim = SingleInstance.claim(dir, BASE, open("A.vfproj"), deadlineIn(400), options());
            long elapsed = millisSince(start);

            assertInstanceOf(Independent.class, claim);
            assertTrue(elapsed < 10_000, elapsed + " ms");
        } finally {
            for (SocketChannel client : held) {
                client.close();
            }
            Files.deleteIfExists(socketFile);
        }
    }

    @Test
    void aSilentOrGarbledConnectionDoesNotHoldUpOtherLaunchers() throws Exception {
        Path dir = configDir();
        // A long read timeout: if connections were served one at a time, the forward below would wait for it.
        Primary primary = primary(dir, InstanceMessage.EMPTY,
                options(LocalSockets.system(), ForegroundGrant.none(), Duration.ofSeconds(10)));
        BlockingQueue<InstanceMessage> received = attachQueue(primary);
        assertEquals(InstanceMessage.EMPTY, received.poll(5, TimeUnit.SECONDS));
        Path socketFile = SingleInstance.socketFile(dir, BASE);

        try (SocketChannel silent = LocalSockets.system().connect(socketFile)) {
            InstanceMessage message = open("A.vfproj");
            long start = System.nanoTime();
            Claim claim = SingleInstance.claim(dir, BASE, message, deadlineIn(5000), options());
            long elapsed = millisSince(start);

            assertInstanceOf(Forwarded.class, claim);
            assertTrue(elapsed < 5000, elapsed + " ms");
            assertEquals(message, received.poll(5, TimeUnit.SECONDS));
        }

        try (SocketChannel garbled = LocalSockets.system().connect(socketFile)) {
            InstanceMessages.parseGreeting(LineIo.readLine(garbled, InstanceMessages.MAX_LINE_BYTES));
            LineIo.writeLine(garbled, "this is not json");
            Ack ack = InstanceMessages.parseAck(LineIo.readLine(garbled, InstanceMessages.MAX_LINE_BYTES));
            assertFalse(ack.ok());
            assertNotNull(ack.error());
        }
        assertInstanceOf(Forwarded.class,
                SingleInstance.claim(dir, BASE, open("B.vfproj"), deadlineIn(1500), options()));
    }

    @Test
    void releaseDeletesTheSocketKeepsTheLockFileAndCanBeRepeated() {
        Path dir = configDir();
        Primary primary = primary(dir, InstanceMessage.EMPTY, options());

        primary.release();
        primary.release();

        assertFalse(primary.listening());
        assertTrue(primary.inbox().isClosing());
        assertFalse(Files.exists(SingleInstance.socketFile(dir, BASE), LinkOption.NOFOLLOW_LINKS));
        assertTrue(Files.exists(SingleInstance.lockFile(dir, BASE)));
        assertTrue(primary(dir, InstanceMessage.EMPTY, options()).listening());
    }

    @Test
    void packagedAndDevelopmentLaunchersUseSeparateFiles() {
        Path dir = configDir();
        assertEquals("launcher", SingleInstance.baseName(true));
        assertEquals("launcher-dev", SingleInstance.baseName(false));

        Claim packaged = SingleInstance.claim(dir, SingleInstance.baseName(true), InstanceMessage.EMPTY,
                deadlineIn(1000), options());
        Claim dev = SingleInstance.claim(dir, SingleInstance.baseName(false), InstanceMessage.EMPTY,
                deadlineIn(1000), options());

        primaries.add(assertInstanceOf(Primary.class, packaged));
        primaries.add(assertInstanceOf(Primary.class, dev));
        assertTrue(Files.exists(dir.resolve("launcher.lock")));
        assertTrue(Files.exists(dir.resolve("launcher-dev.lock")));
    }

    @Test
    void aSettingsFolderThatCannotBeCreatedGivesIndependent() throws IOException {
        Path file = Files.writeString(TestSupport.real(tempDir).resolve("a file"), "x", StandardCharsets.UTF_8);

        Claim claim = SingleInstance.claim(file.resolve("config"), BASE, InstanceMessage.EMPTY, deadlineIn(300),
                options());

        assertInstanceOf(Independent.class, claim);
        assertFalse(warnings.isEmpty());
    }
}
