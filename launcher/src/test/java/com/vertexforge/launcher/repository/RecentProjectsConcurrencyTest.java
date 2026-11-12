package com.vertexforge.launcher.repository;

import com.google.gson.Gson;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.TestSupport.CapturedLog;
import com.vertexforge.launcher.model.RecentEntry;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;
import org.junit.jupiter.api.io.TempDir;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.net.URISyntaxException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Clock;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** AC5 of VK-1678: launchers adding projects at the same time keep every entry. */
class RecentProjectsConcurrencyTest {

    private static final int PER_PROCESS = 20;
    private static final int LIMIT = 500;

    @TempDir
    Path tempDir;

    @Test
    @Timeout(value = 120, unit = TimeUnit.SECONDS)
    void twoLauncherProcessesAddingAtOnceKeepEveryEntry() throws Exception {
        Path root = TestSupport.real(tempDir);
        Path storeFile = root.resolve("config").resolve(RecentProjectsRepository.FILE_NAME);
        Path projectsDir = root.resolve("projects");

        List<Process> children = new ArrayList<>();
        try {
            children.add(startChild(storeFile, projectsDir, "A"));
            children.add(startChild(storeFile, projectsDir, "B"));

            List<BufferedReader> outputs = new ArrayList<>();
            for (Process child : children) {
                BufferedReader output = new BufferedReader(
                        new InputStreamReader(child.getInputStream(), StandardCharsets.UTF_8));
                awaitLine(output, AddProjectsMain.READY, child);
                outputs.add(output);
            }

            // Both are waiting on stdin: release them together, then drain their output so neither can block on it.
            List<CompletableFuture<String>> rest = new ArrayList<>();
            for (int i = 0; i < children.size(); i++) {
                Writer stdin = new OutputStreamWriter(children.get(i).getOutputStream(), StandardCharsets.UTF_8);
                stdin.write("go\n");
                stdin.flush();
                stdin.close();
            }
            for (BufferedReader output : outputs) {
                rest.add(CompletableFuture.supplyAsync(() -> drain(output)));
            }

            for (int i = 0; i < children.size(); i++) {
                Process child = children.get(i);
                assertTrue(child.waitFor(90, TimeUnit.SECONDS), "a child did not finish");
                String output = rest.get(i).get(30, TimeUnit.SECONDS);
                assertEquals(0, child.exitValue(), output);
                assertTrue(output.contains(AddProjectsMain.DONE), output);
            }
        } finally {
            for (Process child : children) {
                child.destroyForcibly();
                child.waitFor(30, TimeUnit.SECONDS);
            }
        }

        JsonObject json = JsonParser.parseString(Files.readString(storeFile)).getAsJsonObject();
        assertEquals(2, json.get("version").getAsInt());
        List<JsonObject> entries = json.getAsJsonArray("projects").asList().stream()
                .map(JsonElement::getAsJsonObject).toList();
        Set<String> ids = entries.stream().map(entry -> entry.get(RecentEntry.ID).getAsString())
                .collect(Collectors.toSet());
        Set<String> names = entries.stream().map(entry -> entry.get(RecentEntry.NAME).getAsString())
                .collect(Collectors.toSet());
        assertEquals(2 * PER_PROCESS, entries.size());
        assertEquals(2 * PER_PROCESS, ids.size());
        assertEquals(2 * PER_PROCESS, names.size());
        for (int i = 0; i < PER_PROCESS; i++) {
            assertTrue(names.contains("A-" + i) && names.contains("B-" + i), names.toString());
        }
        assertTrue(TestSupport.fileNames(storeFile.getParent()).stream().noneMatch(name -> name.endsWith(".tmp")));
    }

    @Test
    @Timeout(value = 60, unit = TimeUnit.SECONDS)
    void twoRepositoriesInOneProcessNeverCollideOnTheFileLock() throws Exception {
        Path root = TestSupport.real(tempDir);
        Path storeFile = root.resolve("config").resolve(RecentProjectsRepository.FILE_NAME);
        CapturedLog log = TestSupport.log(root.resolve("logs"));
        CyclicBarrier start = new CyclicBarrier(2);
        ExecutorService threads = Executors.newFixedThreadPool(2);
        try {
            List<Future<Void>> done = List.of(
                    threads.submit(() -> addMany(storeFile, root, "A", log, start)),
                    threads.submit(() -> addMany(storeFile, root, "B", log, start)));
            for (Future<Void> future : done) {
                // Rethrows an OverlappingFileLockException or a failed save from either thread.
                future.get();
            }
        } finally {
            threads.shutdownNow();
        }

        RecentProjectsView view = newRepository(storeFile, log).load();
        assertEquals(2 * PER_PROCESS, view.entries().size());
    }

    private static Void addMany(Path storeFile, Path root, String prefix, CapturedLog log, CyclicBarrier start)
            throws Exception {
        RecentProjectsRepository repository = newRepository(storeFile, log);
        start.await();
        for (int i = 0; i < PER_PROCESS; i++) {
            repository.add(root.resolve(prefix + "-" + i + ".vfproj"), prefix + "-" + i, LIMIT);
        }
        return null;
    }

    private static RecentProjectsRepository newRepository(Path storeFile, CapturedLog log) {
        return new RecentProjectsRepository(RecentProjectsRepository.openStore(storeFile, Clock.systemUTC(), log.log),
                UUID::randomUUID, Clock.systemUTC(), log.log);
    }

    /**
     * A child JVM with just the launcher's classes, this test's classes and Gson. Surefire runs tests from a
     * manifest-only jar, so {@code java.class.path} cannot simply be copied.
     */
    private static Process startChild(Path storeFile, Path projectsDir, String prefix)
            throws IOException, URISyntaxException {
        String classPath = String.join(java.io.File.pathSeparator,
                codeSource(AddProjectsMain.class), codeSource(RecentProjectsRepository.class), codeSource(Gson.class));
        Path java = Path.of(System.getProperty("java.home"), "bin", TestSupport.isWindows() ? "java.exe" : "java");
        ProcessBuilder builder = new ProcessBuilder(
                java.toString(),
                // Keeps Surefire's backstop: nothing in the child can reach the real ~/.vertexforge.
                "-Duser.home=" + System.getProperty("user.home"),
                "-cp", classPath,
                AddProjectsMain.class.getName(),
                storeFile.toString(), projectsDir.toString(), prefix,
                Integer.toString(PER_PROCESS), Integer.toString(LIMIT));
        builder.redirectErrorStream(true);
        return builder.start();
    }

    private static String codeSource(Class<?> type) throws URISyntaxException {
        return Path.of(type.getProtectionDomain().getCodeSource().getLocation().toURI()).toString();
    }

    private static void awaitLine(BufferedReader output, String expected, Process child) throws IOException {
        StringBuilder seen = new StringBuilder();
        String line;
        while ((line = output.readLine()) != null) {
            if (line.equals(expected)) {
                return;
            }
            seen.append(line).append('\n');
        }
        throw new AssertionError("the child exited before printing " + expected + " (exit "
                + (child.isAlive() ? "?" : child.exitValue()) + "):\n" + seen);
    }

    private static String drain(BufferedReader output) {
        StringBuilder text = new StringBuilder();
        try {
            String line;
            while ((line = output.readLine()) != null) {
                text.append(line).append('\n');
            }
        } catch (IOException e) {
            text.append("<output closed: ").append(e).append(">\n");
        }
        return text.toString();
    }
}
