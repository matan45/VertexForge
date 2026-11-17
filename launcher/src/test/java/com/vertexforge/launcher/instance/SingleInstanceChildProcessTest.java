package com.vertexforge.launcher.instance;

import com.google.gson.Gson;
import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.instance.InstanceRequest.Action;
import com.vertexforge.launcher.instance.SingleInstance.Options;
import com.vertexforge.launcher.instance.SingleInstance.Primary;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.io.InputStream;
import java.net.URISyntaxException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * A second launcher PROCESS hands its request to the running one and exits (VK-1685). The bound is loose on purpose:
 * a cold JVM on a busy CI runner is slow, and {@code SingleInstanceTest} already times the claim itself.
 */
class SingleInstanceChildProcessTest {

    @TempDir
    Path tempDir;

    @Test
    @Timeout(value = 90, unit = TimeUnit.SECONDS)
    void aSecondLauncherProcessForwardsItsRequestAndExits() throws Exception {
        Path root = TestSupport.real(tempDir);
        Path configDir = root.resolve("config");
        List<String> warnings = new CopyOnWriteArrayList<>();
        Options options = new Options(LocalSockets.system(), ForegroundGrant.none(), warnings::add,
                Duration.ofSeconds(1), Duration.ofMillis(25));
        Primary primary = assertInstanceOf(Primary.class, SingleInstance.claim(configDir, SingleInstance.DEV_NAME,
                InstanceMessage.EMPTY, System.nanoTime() + TimeUnit.SECONDS.toNanos(2), options));
        Process child = null;
        try {
            assertTrue(primary.listening(), warnings::toString);
            BlockingQueue<InstanceMessage> received = new LinkedBlockingQueue<>();
            primary.inbox().attach(received::add);
            assertEquals(InstanceMessage.EMPTY, received.poll(5, TimeUnit.SECONDS));
            Path project = root.resolve("Game").resolve("Game.vfproj");

            child = startChild(configDir, project);
            CompletableFuture<String> output = drain(child.getInputStream());

            assertTrue(child.waitFor(60, TimeUnit.SECONDS), "the second launcher did not exit");
            String text = output.get(30, TimeUnit.SECONDS);
            assertEquals(0, child.exitValue(), text);
            assertTrue(text.contains(ClaimMain.FORWARDED), text);
            assertEquals(new InstanceMessage(List.of(new InstanceRequest(Action.OPEN, project)), List.of()),
                    received.poll(5, TimeUnit.SECONDS));
        } finally {
            if (child != null) {
                child.destroyForcibly();
                child.waitFor(30, TimeUnit.SECONDS);
            }
            primary.release();
        }
    }

    /**
     * A child JVM with just the launcher's classes, this test's classes and Gson. Surefire runs tests from a
     * manifest-only jar, so {@code java.class.path} cannot simply be copied.
     */
    private static Process startChild(Path configDir, Path project) throws IOException, URISyntaxException {
        String classPath = String.join(java.io.File.pathSeparator,
                codeSource(ClaimMain.class), codeSource(SingleInstance.class), codeSource(Gson.class));
        Path java = Path.of(System.getProperty("java.home"), "bin", TestSupport.isWindows() ? "java.exe" : "java");
        ProcessBuilder builder = new ProcessBuilder(
                java.toString(),
                // The child makes the real AllowSetForegroundWindow call, as the packaged launcher does.
                "--enable-native-access=ALL-UNNAMED",
                // Keeps Surefire's backstop: nothing in the child can reach the real ~/.vertexforge.
                "-Duser.home=" + System.getProperty("user.home"),
                "-cp", classPath,
                ClaimMain.class.getName(),
                configDir.toString(), SingleInstance.DEV_NAME, project.toString(), "20000");
        builder.redirectErrorStream(true);
        return builder.start();
    }

    private static String codeSource(Class<?> type) throws URISyntaxException {
        return Path.of(type.getProtectionDomain().getCodeSource().getLocation().toURI()).toString();
    }

    private static CompletableFuture<String> drain(InputStream stream) {
        return CompletableFuture.supplyAsync(() -> {
            try (stream) {
                return new String(stream.readAllBytes(), StandardCharsets.UTF_8);
            } catch (IOException e) {
                return "<output closed: " + e + ">";
            }
        });
    }
}
