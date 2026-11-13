package com.vertexforge.launcher.service;

import com.vertexforge.launcher.env.DriveTypes;
import com.vertexforge.launcher.env.LauncherEnvironment;

import java.nio.charset.Charset;
import java.nio.charset.CharsetEncoder;
import java.nio.charset.StandardCharsets;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.function.UnaryOperator;

/**
 * Warnings about where a project is going (VK-1679). They never block Create: the project works there, but something
 * about the place is known to cause trouble later.
 * <ul>
 *   <li>OneDrive syncs every file the editor writes, and can lock or move them while it does.</li>
 *   <li>A network drive is slow, and the project is lost to the editor whenever the drive is.</li>
 *   <li>The editor takes its project path through a narrow {@code main}, so characters outside the system's ANSI code
 *       page arrive as {@code ?} (until Unicode paths, VK-1684).</li>
 * </ul>
 */
public final class LocationWarnings {

    /** Set by OneDrive to its sync folders: the default account, a personal one, a work or school one. */
    static final List<String> ONEDRIVE_VARIABLES = List.of("OneDrive", "OneDriveConsumer", "OneDriveCommercial");

    static final String ONEDRIVE_WARNING = "This folder is synced by OneDrive. OneDrive can lock or move project "
            + "files while the editor uses them; a folder outside OneDrive is safer.";
    static final String NETWORK_WARNING = "This folder is on a network drive. The editor will be slow there, and "
            + "the project is unavailable whenever the drive is.";

    private final UnaryOperator<String> env;
    private final Charset nativeCharset;
    private final DriveTypes driveTypes;

    public LocationWarnings(UnaryOperator<String> env, Charset nativeCharset, DriveTypes driveTypes) {
        this.env = Objects.requireNonNull(env, "env");
        this.nativeCharset = Objects.requireNonNull(nativeCharset, "nativeCharset");
        this.driveTypes = Objects.requireNonNull(driveTypes, "driveTypes");
    }

    /** Warns about nothing: no environment variables, a UTF-8 code page, no drive information. */
    public static LocationWarnings none() {
        return new LocationWarnings(name -> null, StandardCharsets.UTF_8, DriveTypes.unknown());
    }

    public static LocationWarnings forEnvironment(LauncherEnvironment environment) {
        return new LocationWarnings(environment.env(), environment.nativeCharset(), environment.driveTypes());
    }

    /**
     * The warnings for a project whose file will be {@code projectFile}, in the absolute folder {@code location}.
     * Looks at the drive type only; never reads the folder itself.
     */
    public List<String> warningsFor(Path location, Path projectFile) {
        List<String> warnings = new ArrayList<>();
        if (underOneDrive(location)) {
            warnings.add(ONEDRIVE_WARNING);
        }
        if (onNetworkDrive(location)) {
            warnings.add(NETWORK_WARNING);
        }
        unencodable(projectFile.toString()).ifPresent(characters -> warnings.add("The path contains " + characters
                + ", which this system's code page (" + nativeCharset.name() + ") can't represent. The editor may "
                + "not be able to open the project there."));
        return List.copyOf(warnings);
    }

    /** Whether {@code location} is a OneDrive sync folder or inside one. Whole folder names only, ignoring case. */
    boolean underOneDrive(Path location) {
        Path normalized = location.normalize();
        for (String variable : ONEDRIVE_VARIABLES) {
            String value = env.apply(variable);
            if (value == null || value.isBlank()) {
                continue;
            }
            Path root;
            try {
                root = Path.of(value.trim()).normalize();
            } catch (InvalidPathException e) {
                continue;
            }
            // Path.startsWith compares whole names (OneDriveBackup is not under OneDrive), ignoring case on Windows.
            if (root.isAbsolute() && normalized.startsWith(root)) {
                return true;
            }
        }
        return false;
    }

    /** A UNC path, or a drive letter Windows reports as remote. */
    boolean onNetworkDrive(Path location) {
        Path root = location.getRoot();
        if (root == null) {
            return false;
        }
        if (root.toString().startsWith("\\\\")) {
            return true;
        }
        try {
            return driveTypes.kindOf(root) == DriveTypes.Kind.REMOTE;
        } catch (RuntimeException e) {
            // Not knowing is never a reason to warn.
            return false;
        }
    }

    /** The distinct characters of {@code text} the code page can't encode, quoted and listed; empty when none. */
    Optional<String> unencodable(String text) {
        CharsetEncoder encoder = nativeCharset.newEncoder();
        Set<String> missing = new LinkedHashSet<>();
        text.codePoints().forEach(codePoint -> {
            String character = Character.toString(codePoint);
            if (!encoder.canEncode(character)) {
                missing.add("\"" + character + "\"");
            }
        });
        return missing.isEmpty() ? Optional.empty() : Optional.of(String.join(", ", missing));
    }
}
