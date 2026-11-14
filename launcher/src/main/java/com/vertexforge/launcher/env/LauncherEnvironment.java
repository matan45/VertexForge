package com.vertexforge.launcher.env;

import java.nio.charset.Charset;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Clock;
import java.util.Objects;
import java.util.Optional;
import java.util.function.UnaryOperator;

/**
 * Everything the launcher reads from the machine it runs on, gathered in one place so that the logic can run
 * against temp folders, a fixed clock and fake processes (VK-1677).
 *
 * @param clock              the time source for every timestamp the launcher writes
 * @param configDir          the launcher's settings folder ({@code ~/.vertexforge}); the editor keeps its own files
 *                           there too, so the launcher only ever touches its own files in it
 * @param launcherExecutable the packaged launcher's own exe ({@code jpackage.app-path}), from which the bundled
 *                           engine is found (VK-1681); empty when the launcher runs from a jar or
 *                           {@code mvn javafx:run}. The working directory ({@code user.dir}) is never used.
 * @param env                environment-variable lookup; returns {@code null} for an unset variable
 * @param processStarter     starts the editor process
 * @param nativeCharset      the system's ANSI code page ({@code native.encoding}): the editor's narrow {@code main}
 *                           only receives paths this charset can encode (VK-1679)
 * @param driveTypes         tells local drives from network ones (VK-1679)
 */
public record LauncherEnvironment(
        Clock clock,
        Path configDir,
        Optional<Path> launcherExecutable,
        UnaryOperator<String> env,
        ProcessStarter processStarter,
        Charset nativeCharset,
        DriveTypes driveTypes
) {
    public static final String CONFIG_DIR_NAME = ".vertexforge";
    /** Set by jpackage's native launcher to the full path of its exe; absent under {@code java -jar}. */
    public static final String APP_PATH_PROPERTY = "jpackage.app-path";

    public LauncherEnvironment {
        Objects.requireNonNull(clock, "clock");
        Objects.requireNonNull(configDir, "configDir");
        Objects.requireNonNull(launcherExecutable, "launcherExecutable");
        Objects.requireNonNull(env, "env");
        Objects.requireNonNull(processStarter, "processStarter");
        Objects.requireNonNull(nativeCharset, "nativeCharset");
        Objects.requireNonNull(driveTypes, "driveTypes");
    }

    /**
     * The real machine. This is the only place in the launcher that reads {@code user.home},
     * {@code jpackage.app-path}, {@code native.encoding} or the process environment.
     */
    public static LauncherEnvironment system() {
        return new LauncherEnvironment(
                Clock.systemDefaultZone(),
                Paths.get(System.getProperty("user.home"), CONFIG_DIR_NAME),
                systemLauncherExecutable(),
                System::getenv,
                ProcessStarter.system(),
                nativeCharset(System.getProperty("native.encoding")),
                DriveTypes.system());
    }

    /** This process's launcher exe, from {@link #APP_PATH_PROPERTY}; see {@link #launcherExecutable(String)}. */
    public static Optional<Path> systemLauncherExecutable() {
        return launcherExecutable(System.getProperty(APP_PATH_PROPERTY));
    }

    /** The launcher exe named by {@code value}; empty when it is missing, blank, not a valid path or relative. */
    static Optional<Path> launcherExecutable(String value) {
        if (value == null || value.isBlank()) {
            return Optional.empty();
        }
        try {
            Path path = Path.of(value);
            return path.isAbsolute() ? Optional.of(path.normalize()) : Optional.empty();
        } catch (InvalidPathException e) {
            return Optional.empty();
        }
    }

    /** The charset named by {@code native.encoding}; the default charset when it is missing or unknown. */
    static Charset nativeCharset(String name) {
        if (name == null || name.isBlank()) {
            return Charset.defaultCharset();
        }
        try {
            return Charset.forName(name);
        } catch (IllegalArgumentException e) {
            return Charset.defaultCharset();
        }
    }
}
