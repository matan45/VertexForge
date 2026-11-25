package com.vertexforge.launcher.scriptlib;

import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.SortedMap;
import java.util.SortedSet;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

/**
 * The legacy baseline the launcher carries is exactly what the git history holds (VK-1691). Needs the full history
 * (skipped in a shallow clone, as CI's). Regenerate the resource with
 * {@code mvnw -B test -Dtest=LegacyLibraryHashesHistoryTest -Dvertexforge.writeLegacyLibraryHashes=true}.
 */
class LegacyLibraryHashesHistoryTest {

    private static final String WRITE_PROPERTY = "vertexforge.writeLegacyLibraryHashes";

    @Test
    void theBundledSetIsTheGitHistorysUpToTheFrozenCommit() throws IOException {
        String root = System.getProperty("vertexforge.repoRoot");
        assumeTrue(root != null, "vertexforge.repoRoot is not set");
        Path repo = Path.of(root).toAbsolutePath().normalize();
        assumeTrue(LegacyLibraryHashesGenerator.historyAvailable(repo),
                "the git history up to " + LegacyLibraryHashesGenerator.THROUGH + " is not available");

        SortedMap<String, SortedSet<String>> generated = LegacyLibraryHashesGenerator.generate(repo);
        Path source = repo.resolve("launcher/src/main/resources").resolve(LegacyLibraryHashes.RESOURCE.substring(1));
        if (Boolean.getBoolean(WRITE_PROPERTY)) {
            Files.createDirectories(source.getParent());
            Files.write(source, LegacyLibraryHashesGenerator.toJson(generated));
        }

        // Compared parsed, so a checkout that turned the file's LF into CRLF still matches.
        LegacyLibraryHashes committed = LegacyLibraryHashes.parse(Files.readString(source, StandardCharsets.UTF_8));
        assertEquals(LegacyLibraryHashesGenerator.THROUGH, committed.through());
        assertEquals(generated, committed.files());
    }
}
