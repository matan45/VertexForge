package com.vertexforge.launcher.env;

import java.nio.charset.Charset;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Clock;
import java.util.Objects;
import java.util.function.UnaryOperator;

/**
 * Everything the launcher reads from the machine it runs on, gathered in one place so that the logic can run
 * against temp folders, a fixed clock and fake processes (VK-1677).
 *
 * @param clock          the time source for every timestamp the launcher writes
 * @param configDir      the launcher's settings folder ({@code ~/.vertexforge}); the editor keeps its own files
 *                       there too, so the launcher only ever touches its own files in it
 * @param workingDir     the process working directory ({@code user.dir}); the editor search still probes relative
 *                       to it
 * @param env            environment-variable lookup; returns {@code null} for an unset variable
 * @param processStarter starts the editor process
 * @param nativeCharset  the system's ANSI code page ({@code native.encoding}): the editor's narrow {@code main} only
 *                       receives paths this charset can encode (VK-1679)
 * @param driveTypes     tells local drives from network ones (VK-1679)
 */
public record LauncherEnvironment(
        Clock clock,
        Path configDir,
        Path workingDir,
        UnaryOperator<String> env,
        ProcessStarter processStarter,
        Charset nativeCharset,
        DriveTypes driveTypes
) {
    public static final String CONFIG_DIR_NAME = ".vertexforge";

    public LauncherEnvironment {
        Objects.requireNonNull(clock, "clock");
        Objects.requireNonNull(configDir, "configDir");
        Objects.requireNonNull(workingDir, "workingDir");
        Objects.requireNonNull(env, "env");
        Objects.requireNonNull(processStarter, "processStarter");
        Objects.requireNonNull(nativeCharset, "nativeCharset");
        Objects.requireNonNull(driveTypes, "driveTypes");
    }

    /**
     * The real machine. This is the only place in the launcher that reads {@code user.home}, {@code user.dir},
     * {@code native.encoding} or the process environment.
     */
    public static LauncherEnvironment system() {
        return new LauncherEnvironment(
                Clock.systemDefaultZone(),
                Paths.get(System.getProperty("user.home"), CONFIG_DIR_NAME),
                Path.of(System.getProperty("user.dir")),
                System::getenv,
                ProcessStarter.system(),
                nativeCharset(System.getProperty("native.encoding")),
                DriveTypes.system());
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
