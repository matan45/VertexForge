package com.vertexforge.launcher.service;

import java.nio.file.Path;
import java.util.HashSet;
import java.util.Locale;
import java.util.Optional;
import java.util.Set;

/**
 * The names Windows and the engine can handle as a project folder and a {@code .vfproj} file (VK-1679). Every check
 * here is a pure string check, so the dialog runs it on every keystroke and the creator runs it again, authoritatively,
 * before it writes anything.
 * <p>
 * The name is checked exactly as typed. Above all it is never trimmed: {@code Path.of} throws on a trailing space and
 * Windows silently strips a trailing dot, so both must be caught here, before any path is built from the name.
 */
public final class ProjectNameRules {

    /** Windows' {@code MAX_PATH} is 260 characters including the terminating NUL. */
    public static final int MAX_PATH_CHARS = 259;
    /**
     * Characters kept free below {@link #MAX_PATH_CHARS} for what the editor writes next to the template files later:
     * a {@code .vfmeta} sidecar (7), an imported take ({@code _<take>.vfAnim}), a {@code sectors.bak} folder.
     */
    public static final int PATH_MARGIN = 40;
    /** The characters Windows never allows in a file name (control characters have a rule of their own). */
    static final String FORBIDDEN_CHARACTERS = "<>:\"/\\|?*";

    /**
     * Windows device names, reserved in every folder, with or without an extension. COM0 and LPT0 are not in
     * Microsoft's list but are rejected anyway; the superscript digits ¹ ² ³ count as digits in COM and LPT names.
     */
    private static final Set<String> RESERVED_NAMES = reservedNames();

    public enum Rule {
        EMPTY,
        CONTROL_CHARACTER,
        FORBIDDEN_CHARACTER,
        LEADING_SPACE,
        TRAILING_SPACE,
        TRAILING_DOT,
        RESERVED_NAME,
        RESERVED_NAME_WITH_EXTENSION,
        PATH_TOO_LONG
    }

    /** A rule the name breaks, and what to tell the user. */
    public record Problem(Rule rule, String message) {
    }

    private ProjectNameRules() {
        // Utility class - prevent instantiation
    }

    /** The first rule {@code name} breaks, checked in the order of {@link Rule}, or empty when it is a valid name. */
    public static Optional<Problem> checkName(String name) {
        if (name == null || name.isEmpty()) {
            return problem(Rule.EMPTY, "Enter a project name.");
        }
        for (int i = 0; i < name.length(); i++) {
            if (Character.isISOControl(name.charAt(i))) {
                return problem(Rule.CONTROL_CHARACTER,
                        "The project name can't contain control characters such as tabs or line breaks.");
            }
        }
        for (int i = 0; i < name.length(); i++) {
            char c = name.charAt(i);
            if (FORBIDDEN_CHARACTERS.indexOf(c) >= 0) {
                return problem(Rule.FORBIDDEN_CHARACTER, "The project name can't contain \"" + c
                        + "\". These characters aren't allowed: < > : \" / \\ | ? *");
            }
        }
        if (name.charAt(0) == ' ') {
            return problem(Rule.LEADING_SPACE, "The project name can't start with a space.");
        }
        char last = name.charAt(name.length() - 1);
        if (last == ' ') {
            return problem(Rule.TRAILING_SPACE, "The project name can't end with a space.");
        }
        if (last == '.') {
            return problem(Rule.TRAILING_DOT, "The project name can't end with a dot.");
        }

        String device = reservedDeviceName(name);
        if (device != null) {
            if (name.indexOf('.') < 0) {
                return problem(Rule.RESERVED_NAME, "\"" + name
                        + "\" is reserved by Windows for a device and can't be used as a project name.");
            }
            return problem(Rule.RESERVED_NAME_WITH_EXTENSION, "\"" + name
                    + "\" can't be used: Windows reserves the device name \"" + device
                    + "\" even when an extension follows.");
        }
        return Optional.empty();
    }

    /**
     * Whether the project's deepest file fits in {@link #MAX_PATH_CHARS} with {@link #PATH_MARGIN} to spare: the
     * engine is not long-path aware, so a file it cannot reach would break the project later, not now.
     *
     * @param location an absolute folder
     * @param name     a name that passed {@link #checkName}
     */
    public static Optional<Problem> checkPathLength(Path location, String name, ProjectTemplate template) {
        int deepestPath = location.normalize().resolve(name).toString().length() + 1
                + template.deepestRelativeLength(name);
        int allowed = MAX_PATH_CHARS - PATH_MARGIN;
        if (deepestPath <= allowed) {
            return Optional.empty();
        }
        return problem(Rule.PATH_TOO_LONG, "This location and name make a path too long for Windows: the deepest "
                + "project file would be " + deepestPath + " characters long, " + (deepestPath - allowed)
                + " more than the " + allowed + " allowed (Windows allows " + MAX_PATH_CHARS + "; " + PATH_MARGIN
                + " are kept free for files the editor adds later). Shorten the name or choose a shorter location.");
    }

    /**
     * The device name {@code name} stands for, in upper case, or {@code null}. Windows compares the text before the
     * first dot, without its trailing spaces and ignoring case, so {@code aux.txt} and {@code nul .tar.gz} are devices.
     */
    static String reservedDeviceName(String name) {
        int dot = name.indexOf('.');
        String base = dot < 0 ? name : name.substring(0, dot);
        int end = base.length();
        while (end > 0 && base.charAt(end - 1) == ' ') {
            end--;
        }
        String candidate = base.substring(0, end).toUpperCase(Locale.ROOT);
        return RESERVED_NAMES.contains(candidate) ? candidate : null;
    }

    private static Optional<Problem> problem(Rule rule, String message) {
        return Optional.of(new Problem(rule, message));
    }

    private static Set<String> reservedNames() {
        Set<String> names = new HashSet<>(Set.of("CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"));
        String digits = "0123456789¹²³";
        for (char digit : digits.toCharArray()) {
            names.add("COM" + digit);
            names.add("LPT" + digit);
        }
        return Set.copyOf(names);
    }
}
