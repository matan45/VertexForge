package com.vertexforge.launcher.util;

import java.util.Optional;

/**
 * The form of a relative path that an engine's own files name: a template's paths (VK-1689, {@code template.json})
 * and the project folders an engine rebuilds (VK-1695, {@code engine.json} {@code projectCacheDirs}). Forward slashes,
 * no empty, {@code .} or {@code ..} part, and only characters (and endings) Windows allows in a file name, so the text
 * names exactly one place below its base on every machine and can never climb out of it. Pure.
 */
public final class RelativePathRules {

    /** The editor's own folder in a project folder: its lock, Play-mode saves and config (VK-1682, VK-1684). */
    public static final String EDITOR_STATE_DIR = ".vertexforge";

    /** Characters Windows refuses in a file name, besides control characters; '/' separates the names. */
    static final String FORBIDDEN_CHARACTERS = "<>:\"|?*\\";

    /** Why a text is not such a path, in the order the parts are checked. */
    public enum Problem {
        /** The text is empty. */
        EMPTY,
        /** It starts with '/', so it is not relative. */
        NOT_RELATIVE,
        /** A part is empty ({@code a//b}, a trailing '/'), {@code .} or {@code ..}. */
        BAD_PART,
        /** A part holds a character Windows does not allow in a file name (this covers {@code C:} and '\'). */
        FORBIDDEN_CHARACTER,
        /** A part ends with a space or a dot, which Windows drops from a file name. */
        BAD_ENDING
    }

    private RelativePathRules() {
        // Utility class - prevent instantiation
    }

    /**
     * Why {@code value} is not a relative path of this form, or empty when it is. The parts are checked left to right,
     * each one for {@link Problem#BAD_PART}, then {@link Problem#FORBIDDEN_CHARACTER}, then {@link Problem#BAD_ENDING},
     * so the first bad part decides.
     */
    public static Optional<Problem> problem(String value) {
        if (value.isEmpty()) {
            return Optional.of(Problem.EMPTY);
        }
        if (value.startsWith("/")) {
            return Optional.of(Problem.NOT_RELATIVE);
        }
        for (String part : value.split("/", -1)) {
            if (part.isEmpty() || part.equals(".") || part.equals("..")) {
                return Optional.of(Problem.BAD_PART);
            }
            for (int i = 0; i < part.length(); i++) {
                char c = part.charAt(i);
                if (Character.isISOControl(c) || FORBIDDEN_CHARACTERS.indexOf(c) >= 0) {
                    return Optional.of(Problem.FORBIDDEN_CHARACTER);
                }
            }
            char last = part.charAt(part.length() - 1);
            if (last == ' ' || last == '.') {
                return Optional.of(Problem.BAD_ENDING);
            }
        }
        return Optional.empty();
    }

    /** Whether {@code value} is a relative path of this form. */
    public static boolean isValid(String value) {
        return problem(value).isEmpty();
    }

    /** Whether a relative path is, or lies in, {@link #EDITOR_STATE_DIR} (its first part, ignoring case). */
    public static boolean isEditorState(String relative) {
        int slash = relative.indexOf('/');
        String first = slash < 0 ? relative : relative.substring(0, slash);
        return first.equalsIgnoreCase(EDITOR_STATE_DIR);
    }
}
