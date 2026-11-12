package com.vertexforge.launcher;

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

    /** An environment rooted in temp folders, with the given environment variables and process starter. */
    public static LauncherEnvironment environment(Path configDir, Path workingDir, Clock clock,
                                                  Map<String, String> variables, ProcessStarter starter) {
        return new LauncherEnvironment(clock, configDir, workingDir, variables::get, starter);
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
