package com.vertexforge.launcher.project;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;

import java.util.List;
import java.util.Objects;

/**
 * The editor's rules for the members of a {@code .vfproj} (VK-1686): a twin of
 * {@code ProjectSerialization::structureError} ({@code VFEngine/utilities/serialization/ProjectSerialization.cpp}),
 * with the editor's own words, so a row marked Invalid says exactly why the editor would refuse the project (it exits
 * with code 3). The launcher's reader ({@link ProjectFileReader}) requires less, so that such a project can still be
 * listed and shown as Invalid.
 */
public final class EditorProjectRules {

    /** The members the editor requires to be strings, in the order it checks them. */
    static final List<String> REQUIRED_STRINGS =
            List.of("schemaVersion", "projectName", "version", "workingDirectory", "startupScene");

    /** The members the editor also requires to be non-empty, in the order it checks them. */
    static final List<String> REQUIRED_NON_EMPTY = List.of("projectName", "version", "workingDirectory", "startupScene");

    private EditorProjectRules() {
        // Utility class - prevent instantiation
    }

    /**
     * The editor's message for the first member it would refuse, or {@code null} when every member passes. The
     * {@code schemaVersion} text itself is checked by {@link SchemaVersion#parse}.
     */
    public static String structureProblem(JsonObject root) {
        Objects.requireNonNull(root, "root");
        for (String field : REQUIRED_STRINGS) {
            JsonElement element = root.get(field);
            if (element == null || !element.isJsonPrimitive() || !element.getAsJsonPrimitive().isString()) {
                return "Invalid project file: missing or invalid '" + field + "' field";
            }
        }
        for (String field : REQUIRED_NON_EMPTY) {
            if (root.get(field).getAsString().isEmpty()) {
                return "Invalid project file: '" + field + "' cannot be empty";
            }
        }
        return null;
    }

    /** The editor's message for a {@code schemaVersion} that {@link SchemaVersion#parse} refuses. */
    public static String schemaFormatProblem(String schemaVersion) {
        return "Invalid schema version format: '" + schemaVersion + "' (expected MAJOR.MINOR)";
    }
}
