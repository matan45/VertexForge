package com.vertexforge.launcher.service;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

/**
 * The step that renames a finished staging folder into the project folder (VK-1679). Tests replace it to inject
 * failures; production always uses {@link #SYSTEM}.
 */
@FunctionalInterface
interface DirectoryMover {

    /**
     * {@code Files.move} with NO options. On Windows that is a single {@code MoveFileEx(source, target, 0)}: a rename
     * that never replaces anything and fails with {@code FileAlreadyExistsException} when the target exists, even when
     * it appeared after Java's own check. {@code ATOMIC_MOVE} would pass {@code MOVEFILE_REPLACE_EXISTING} instead, and
     * {@code REPLACE_EXISTING} deletes an existing empty target first; neither is ever used here.
     */
    DirectoryMover SYSTEM = (source, target) -> Files.move(source, target);

    void move(Path source, Path target) throws IOException;
}
