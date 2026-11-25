package com.vertexforge.launcher.engine;

import com.vertexforge.launcher.model.RecentEntry;

import java.nio.file.Path;
import java.util.Objects;
import java.util.Optional;
import java.util.function.UnaryOperator;

/**
 * The {@code VERTEXFORGE_EDITOR_PATH} environment variable (VK-1681). When it names an editor that checks OK, every
 * project opens with it and the window says so; it is never saved and never bound to a project. A value that does
 * not work is ignored, with a warning.
 *
 * @param rawValue   the variable's value as set
 * @param executable the editor it names, or {@code null} when the value is not an absolute path
 */
public record EditorOverride(String rawValue, Path executable) {

    public static final String ENV_VAR = "VERTEXFORGE_EDITOR_PATH";

    public EditorOverride {
        Objects.requireNonNull(rawValue, "rawValue");
    }

    /** The override, when the variable is set to something other than blanks. */
    public static Optional<EditorOverride> from(UnaryOperator<String> env) {
        String value = env.apply(ENV_VAR);
        if (value == null || value.isBlank()) {
            return Optional.empty();
        }
        return Optional.of(new EditorOverride(value, RecentEntry.absolutePath(unquote(value.strip()))));
    }

    /** {@code "C:\Program Files\…\Editor.exe"} as people often set it, quotes included. */
    static String unquote(String value) {
        if (value.length() >= 2 && value.startsWith("\"") && value.endsWith("\"")) {
            return value.substring(1, value.length() - 1).strip();
        }
        return value;
    }
}
