package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.service.ProjectCacheCleaner.CacheFolder;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** What the VK-1695 dialogs say and accept, without the toolkit. */
class ProjectActionPromptsTest {

    private static final Path FOLDER = Path.of("projects", "Game");

    @Test
    void onlyTheExactNameConfirmsADelete() {
        DeletePrompt prompt = new DeletePrompt("Game", FOLDER, 1024, 1, 1, List.of());

        assertTrue(prompt.matches("Game"));
        for (String typed : new String[] {"game", "Game ", " Game", "", null, "Gam"}) {
            assertFalse(prompt.matches(typed), String.valueOf(typed));
        }
    }

    @Test
    void theDeleteSummaryCountsInWords() {
        assertEquals("1 file in 1 folder, 1.0 KB", new DeletePrompt("Game", FOLDER, 1024, 1, 1, List.of()).summary());
        assertEquals("120 files in 0 folders, 0 bytes",
                new DeletePrompt("Game", FOLDER, 0, 120, 0, List.of()).summary());
        assertTrue(new DeletePrompt("Game", FOLDER, 0, 0, 0, List.of()).confirmLabel().contains("Game"));
    }

    @Test
    void theCleanPromptListsWhatGoesAndWhatStays() {
        CacheFolder present = new CacheFolder("scripts/compiled", "assets/scripts/compiled",
                FOLDER.resolve("assets/scripts/compiled"), CacheFolder.State.PRESENT, 2048, 3, "");
        CacheFolder missing = new CacheFolder("build", "assets/build", FOLDER.resolve("assets/build"),
                CacheFolder.State.MISSING, 0, 0, "");
        CacheFolder skipped = new CacheFolder("out", "assets/out", FOLDER.resolve("assets/out"),
                CacheFolder.State.SKIPPED, 0, 0, "it is a link; what it points to is left alone");

        CleanCachesPrompt prompt = new CleanCachesPrompt("Game", "1.0.0 Release", List.of(present, missing, skipped));

        assertEquals(List.of(present), prompt.present());
        assertEquals(List.of(skipped), prompt.skipped());
        assertEquals(2048, prompt.totalBytes());
        assertEquals(List.of("assets/scripts/compiled  —  2.0 KB (3 files)"), prompt.presentLines());
        assertEquals(List.of("assets/out: it is a link; what it points to is left alone"), prompt.skippedLines());
        assertTrue(prompt.explanation().startsWith("1.0.0 Release rebuilds these folders"), prompt.explanation());
    }

    @Test
    void aCopyPromptIsAnOpenACopyUnlessItSaysOtherwise() {
        CopyNamePrompt openCopy = new CopyNamePrompt("Game", FOLDER.getParent(), "Game-1.1.0", null);
        CopyNamePrompt duplicate = new CopyNamePrompt("Game", FOLDER.getParent(), "Game-copy", null,
                CopyNamePrompt.Purpose.DUPLICATE);

        assertEquals(CopyNamePrompt.Purpose.OPEN_COPY, openCopy.purpose());
        assertEquals("Open a Copy", openCopy.purpose().title());
        assertEquals("Copy \"Game\" and open the copy", openCopy.header());
        assertTrue(openCopy.intro().contains("and the copy opens"), openCopy.intro());

        assertEquals("Duplicate Project", duplicate.purpose().title());
        assertEquals("Duplicate", duplicate.purpose().button());
        assertEquals("Duplicate \"Game\"", duplicate.header());
        assertTrue(duplicate.intro().contains("it does not open"), duplicate.intro());
    }
}
