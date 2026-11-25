package com.vertexforge.launcher.store;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

/**
 * The step that replaces a store's file with the fully written temp file. Tests replace it to inject failures
 * (VK-1678); production always uses {@link #SYSTEM}.
 */
@FunctionalInterface
interface AtomicMover {

    /**
     * {@code ATOMIC_MOVE}: on Windows this is {@code MoveFileEx} with {@code MOVEFILE_REPLACE_EXISTING}, so a reader
     * sees either the old file or the new one, never a partial one. There is no non-atomic fallback.
     */
    AtomicMover SYSTEM = (source, target) -> Files.move(source, target, StandardCopyOption.ATOMIC_MOVE);

    void move(Path source, Path target) throws IOException;
}
