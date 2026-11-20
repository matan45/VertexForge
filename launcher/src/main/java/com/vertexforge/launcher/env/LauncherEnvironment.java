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
 *                           engine is found (VK-1681); empty when the launcher runs outside the app image (the unit
 *                           tests, {@code mvn javafx:run}). The working directory ({@code user.dir}) is only read by
 *                           {@link #systemWorkingDirectory()}, to resolve relative command-line paths (VK-1685).
 * @param env                environment-variable lookup; returns {@code null} for an unset variable
 * @param processStarter     starts the editor process
 * @param nativeCharset      the system's ANSI code page ({@code native.encoding}): an editor older than VK-1684 only
 *                           receives paths this charset can encode, and parts of its output are in it
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
    /** Set by jpackage's native launcher to the full path of its exe; absent outside the app image. */
    public static final String APP_PATH_PROPERTY = "jpackage.app-path";
    /** Set by jpackage's native launcher to the image's {@code --app-version}, the repo's VERSION (VK-1692). */
    public static final String APP_VERSION_PROPERTY = "jpackage.app-version";

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
     * {@code jpackage.app-path}, {@code jpackage.app-version}, {@code java.home}, {@code native.encoding} or the process
     * environment; {@code user.dir} is read only by {@link #systemWorkingDirectory()}.
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

    /** The app image's version ({@link #APP_VERSION_PROPERTY}); empty outside the app image or when blank. */
    public static Optional<String> systemAppVersion() {
        String value = System.getProperty(APP_VERSION_PROPERTY);
        return value == null || value.isBlank() ? Optional.empty() : Optional.of(value);
    }

    /** The runtime this process runs on ({@code java.home}); in the app image, its {@code runtime} folder. */
    public static Path systemJavaHome() {
        return Path.of(System.getProperty("java.home"));
    }

    /**
     * This process's working directory, as an absolute path: relative paths on the command line are resolved against
     * it before they are handed to a launcher that may run in another folder (VK-1685). The only reader of
     * {@code user.dir}.
     */
    public static Path systemWorkingDirectory() {
        return Path.of("").toAbsolutePath();
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
