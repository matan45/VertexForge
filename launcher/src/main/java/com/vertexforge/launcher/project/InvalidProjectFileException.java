package com.vertexforge.launcher.project;

import java.util.Objects;
import java.util.OptionalInt;

/**
 * A {@code .vfproj} that the launcher refuses (VK-1685). The message is the whole text shown to the user, for
 * example {@code Not valid JSON at line 3, column 8: expected ':'}; a JSON error also carries its line and column.
 */
public final class InvalidProjectFileException extends Exception {

    private final String reason;
    private final int line;
    private final int column;

    /** A refusal without a place in the file. */
    public InvalidProjectFileException(String reason) {
        super(Objects.requireNonNull(reason, "reason"));
        this.reason = reason;
        this.line = 0;
        this.column = 0;
    }

    /** A refusal at a place in the file; {@code line} and {@code column} count from 1, as Gson reports them. */
    public InvalidProjectFileException(String reason, int line, int column) {
        super(Objects.requireNonNull(reason, "reason"));
        if (line < 1 || column < 1) {
            throw new IllegalArgumentException("line and column count from 1");
        }
        this.reason = reason;
        this.line = line;
        this.column = column;
    }

    /** The text shown to the user; the same as {@link #getMessage()}. */
    public String reason() {
        return reason;
    }

    /** The line of the error (from 1), when the refusal has a place in the file. */
    public OptionalInt line() {
        return line == 0 ? OptionalInt.empty() : OptionalInt.of(line);
    }

    /** The column of the error (from 1), when the refusal has a place in the file. */
    public OptionalInt column() {
        return column == 0 ? OptionalInt.empty() : OptionalInt.of(column);
    }
}
