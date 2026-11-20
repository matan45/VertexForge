package com.vertexforge.launcher.service;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

/**
 * How much a folder's drive can still take (VK-1690), checked before a backup or a copy writes anything. Tests replace
 * it; production always uses {@link #SYSTEM}.
 */
@FunctionalInterface
public interface SpaceProbe {

    /** {@code FileStore.getUsableSpace()}: the bytes this user may still write there (quotas included). */
    SpaceProbe SYSTEM = folder -> Files.getFileStore(folder).getUsableSpace();

    /**
     * @throws IOException when the drive cannot say; the job then goes ahead without the check
     */
    long usableBytes(Path folder) throws IOException;
}
