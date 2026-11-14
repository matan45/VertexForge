package com.vertexforge.launcher;

import com.vertexforge.launcher.env.DriveTypes;
import com.vertexforge.launcher.env.LauncherEnvironment;
import com.vertexforge.launcher.env.ProcessStarter;
import com.vertexforge.launcher.util.LauncherLog;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.PrintStream;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Clock;
import java.time.Instant;
import java.time.ZoneId;
import java.time.ZoneOffset;
import java.util.Arrays;
import java.util.Locale;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/** Shared fakes for the launcher's unit tests. Nothing here touches the real user folder. */
public final class TestSupport {

    public static final Instant T0 = Instant.parse("2026-01-02T03:04:05Z");

    private TestSupport() {
    }

    public static Clock fixedClock() {
        return Clock.fixed(T0, ZoneOffset.UTC);
    }

    /** A log in {@code dir} whose console output is captured. */
    public static CapturedLog log(Path dir) {
        return new CapturedLog(dir.resolve(LauncherLog.FILE_NAME), LauncherLog.DEFAULT_MAX_BYTES);
    }

    /**
     * The real path of a test folder. The CI runner's temp folder is an 8.3 short path ({@code RUNNER~1}), while
     * created projects are listed by their real path, so every comparison must start from the real one.
     */
    public static Path real(Path path) {
        try {
            return path.toRealPath();
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    public static boolean isWindows() {
        return System.getProperty("os.name", "").toLowerCase(Locale.ROOT).startsWith("windows");
    }

    /**
     * A drive letter (Z down to D) that does not exist on this machine right now, or empty when there is none or this
     * is not Windows. A and B are skipped: virtual floppy drives stall on some machines.
     */
    public static Optional<Path> absentDriveRoot() {
        if (!isWindows()) {
            return Optional.empty();
        }
        Set<String> roots = Arrays.stream(File.listRoots())
                .map(root -> root.getPath().toUpperCase(Locale.ROOT))
                .collect(Collectors.toSet());
        for (char letter = 'Z'; letter >= 'D'; letter--) {
            String root = letter + ":\\";
            if (!roots.contains(root) && !Files.exists(Path.of(root))) {
                return Optional.of(Path.of(root));
            }
        }
        return Optional.empty();
    }

    /** The names of the files directly in {@code dir}. */
    public static Set<String> fileNames(Path dir) {
        try (Stream<Path> files = Files.list(dir)) {
            return files.map(path -> path.getFileName().toString()).collect(Collectors.toSet());
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    /**
     * An environment rooted in temp folders, with the given environment variables and process starter, a UTF-8 code
     * page (no code-page warnings), no drive information (no native calls) and no launcher exe (not packaged).
     */
    public static LauncherEnvironment environment(Path configDir, Clock clock, Map<String, String> variables,
                                                  ProcessStarter starter) {
        return environment(configDir, Optional.empty(), clock, variables, starter);
    }

    /** As {@link #environment(Path, Clock, Map, ProcessStarter)}, for a launcher started from this exe. */
    public static LauncherEnvironment environment(Path configDir, Optional<Path> launcherExecutable, Clock clock,
                                                  Map<String, String> variables, ProcessStarter starter) {
        return new LauncherEnvironment(clock, configDir, launcherExecutable, variables::get, starter,
                StandardCharsets.UTF_8, DriveTypes.unknown());
    }

    /**
     * A fake {@code Editor.exe} in {@code dir}. On Windows every file a test creates can be "executed" (its ACL
     * allows it, and {@code setExecutable} does nothing), so it passes {@code Files.isExecutable}.
     */
    public static Path fakeEditor(Path dir) {
        try {
            Path editor = Files.createDirectories(dir).resolve("Editor.exe");
            Files.writeString(editor, "fake");
            editor.toFile().setExecutable(true);
            return editor;
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    /**
     * An {@code engine.json} in {@code dir}, as {@code tools/engine_version.lua} writes it (VK-1680), with the given
     * configuration and working directory (relative to {@code dir}).
     */
    public static Path writeManifest(Path dir, String configuration, String workingDirectory) {
        String text = """
                {
                  "manifestVersion": 1,
                  "engineVersion": "1.0.0",
                  "assetFormatVersion": "1.0.0",
                  "projectSchemaVersion": "1.0",
                  "pluginApiVersion": 25,
                  "configuration": "%s",
                  "gitCommit": null,
                  "editorExe": "Editor.exe",
                  "workingDirectory": "%s",
                  "templatesDir": "../../../../assets/templates",
                  "scriptLibDir": "../../../../assets/scripts/lib"
                }
                """.formatted(configuration, workingDirectory);
        try {
            return Files.writeString(Files.createDirectories(dir).resolve("engine.json"), text, StandardCharsets.UTF_8);
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    /**
     * A Development build laid out as in this repository: {@code repo/bin/Editor/Development/x64/Editor.exe} with an
     * {@code engine.json} whose working directory {@code ../../../../VFEngine/editor} exists.
     *
     * @return the editor
     */
    public static Path devEngine(Path repo) {
        Path exeDir = repo.resolve("bin/Editor/Development/x64");
        Path editor = fakeEditor(exeDir);
        writeManifest(exeDir, "Development", "../../../../VFEngine/editor");
        try {
            Files.createDirectories(repo.resolve("VFEngine/editor"));
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
        return editor;
    }

    /**
     * A release folder as {@code tools/package_release.lua} lays it out: the launcher app image
     * {@code VertexForge Launcher/} (with {@code app/} and {@code runtime/}) beside
     * {@code bin/Editor/Release/x64/Editor.exe} and its {@code engine.json} (working directory {@code "."}).
     *
     * @return the launcher's exe
     */
    public static Path packagedLauncher(Path releaseRoot) {
        try {
            Path image = Files.createDirectories(releaseRoot.resolve("VertexForge Launcher"));
            Files.createDirectories(image.resolve("app"));
            Files.createDirectories(image.resolve("runtime"));
            Path launcher = Files.writeString(image.resolve("VertexForge Launcher.exe"), "fake launcher");
            Path exeDir = releaseRoot.resolve("bin/Editor/Release/x64");
            fakeEditor(exeDir);
            writeManifest(exeDir, "Release", ".");
            return launcher;
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    /** A clock that moves one second forward on every read, so consecutive timestamps never tie. */
    public static final class TickingClock extends Clock {
        private Instant next;

        public TickingClock(Instant start) {
            this.next = start;
        }

        @Override
        public ZoneId getZone() {
            return ZoneOffset.UTC;
        }

        @Override
        public Clock withZone(ZoneId zone) {
            throw new UnsupportedOperationException();
        }

        @Override
        public synchronized Instant instant() {
            Instant now = next;
            next = next.plusSeconds(1);
            return now;
        }
    }

    public static final class CapturedLog {
        private final ByteArrayOutputStream console = new ByteArrayOutputStream();
        public final LauncherLog log;

        public CapturedLog(Path file, long maxBytes) {
            this.log = new LauncherLog(file, fixedClock(), maxBytes, LauncherLog.DEFAULT_BACKUPS,
                    new PrintStream(console, true, StandardCharsets.UTF_8));
        }

        public String console() {
            return console.toString(StandardCharsets.UTF_8);
        }
    }
}
