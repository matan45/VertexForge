package com.vertexforge.launcher;

import com.vertexforge.launcher.env.LauncherEnvironment;
import com.vertexforge.launcher.env.ProcessStarter;
import com.vertexforge.launcher.util.LauncherLog;

import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.time.Clock;
import java.time.Instant;
import java.time.ZoneId;
import java.time.ZoneOffset;
import java.util.Map;

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
