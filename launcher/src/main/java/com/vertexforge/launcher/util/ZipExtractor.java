package com.vertexforge.launcher.util;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Extracts a zip archive into a folder. */
public final class ZipExtractor {

    private ZipExtractor() {
        // Utility class - prevent instantiation
    }

    /**
     * Extracts every entry of {@code zip} below {@code targetDir} and closes the stream.
     * An entry that would land outside {@code targetDir} (zip slip) fails with an {@link IOException};
     * an existing file is never overwritten.
     */
    public static void extract(InputStream zip, Path targetDir) throws IOException {
        try (ZipInputStream zipIn = new ZipInputStream(zip)) {
            ZipEntry entry;
            while ((entry = zipIn.getNextEntry()) != null) {
                try {
                    Path targetPath = targetDir.resolve(entry.getName());

                    // Security: Prevent zip slip attack
                    if (!targetPath.normalize().startsWith(targetDir.normalize())) {
                        throw new IOException("Invalid zip entry: " + entry.getName());
                    }

                    if (entry.isDirectory()) {
                        Files.createDirectories(targetPath);
                    } else {
                        // Ensure parent directory exists
                        Files.createDirectories(targetPath.getParent());
                        Files.copy(zipIn, targetPath);
                    }
                } finally {
                    zipIn.closeEntry();
                }
            }
        }
    }
}
