package com.vertexforge.launcher.service;

import java.util.Locale;
import java.util.Optional;

/**
 * The exit codes of {@code Editor.exe}, the editor's half of the launch contract (VK-1682).
 *
 * <p>The C++ twin is {@code VFEngine/utilities/launch/EditorExitCode.hpp}; {@code VFEngine/tests/test_editor_launch.cpp}
 * pins that both carry the same values, by reading this file. Codes 2 to 5 arrive within moments of the start: the
 * editor checks its arguments, the project file and the project lock before it opens its window.
 *
 * <p>A crashed editor exits with its Windows exception code instead (for example {@code 0xC0000005}, negative as a
 * Java {@code int}). In a Debug build the CRT's assertion dialog can also end the editor with 3, so the editor's
 * {@code --launch-status} file, which says {@code failed} with code 3 only when the editor refused the project itself,
 * is the source to trust when both are available.
 */
public enum EditorExitCode {
    OK(0, "the editor closed normally"),
    BAD_ARGUMENTS(2, "the editor rejected its command line"),
    PROJECT_LOAD_FAILED(3, "the project failed to load"),
    PROJECT_SCHEMA_TOO_NEW(4, "the project was saved by a newer VertexForge"),
    PROJECT_ALREADY_OPEN(5, "the project is already open in another editor");

    private final int code;
    private final String meaning;

    EditorExitCode(int code, String meaning) {
        this.code = code;
        this.meaning = meaning;
    }

    public int code() {
        return code;
    }

    /** What the code means, as a lower-case phrase that completes a sentence. */
    public String meaning() {
        return meaning;
    }

    public static Optional<EditorExitCode> fromCode(int code) {
        for (EditorExitCode value : values()) {
            if (value.code == code) {
                return Optional.of(value);
            }
        }
        return Optional.empty();
    }

    /**
     * {@code "code 3: the project failed to load"} for a documented code, {@code "code 1"} for any other, and
     * {@code "code -1073741819 (0xC0000005)"} for a negative one, which is a Windows exception code.
     */
    public static String describe(int code) {
        return fromCode(code)
                .map(value -> "code " + code + ": " + value.meaning)
                .orElseGet(() -> code < 0
                        ? String.format(Locale.ROOT, "code %d (0x%08X)", code, code)
                        : "code " + code);
    }
}
