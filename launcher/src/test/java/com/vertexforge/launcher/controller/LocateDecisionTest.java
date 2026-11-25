package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.project.PickedProject;
import com.vertexforge.launcher.project.ProjectFile;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.List;
import java.util.Locale;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

/** What Locate… does with a picked file (VK-1686). Pure: no files are read. */
class LocateDecisionTest {

    private static final Path ROOT = Path.of(System.getProperty("java.io.tmpdir")).toAbsolutePath();

    private static RecentEntry entry(String name, String folder) {
        return RecentEntry.create(UUID.randomUUID(), ROOT.resolve(folder).resolve(name + ".vfproj"), name,
                TestSupport.T0);
    }

    private static PickedProject.Found found(Path file, String name) {
        return new PickedProject.Found(file, new ProjectFile(name, "1.0", "VertexForge 1.0.0"));
    }

    @Test
    void theMovedProjectIsRelinkedWithoutAQuestion() {
        RecentEntry lost = entry("Game", "Old");
        PickedProject.Found moved = found(ROOT.resolve("New/Game.vfproj"), "Game");

        LocateDecision.Relink relink = assertInstanceOf(LocateDecision.Relink.class,
                LocateDecision.decide(lost, moved, List.of(lost)));

        assertEquals(moved, relink.found());
        assertNull(relink.confirmation());
    }

    @Test
    void aProjectWithAnotherNameIsRelinkedOnlyAfterAQuestion() {
        RecentEntry lost = entry("Game", "Old");

        LocateDecision.Relink relink = assertInstanceOf(LocateDecision.Relink.class,
                LocateDecision.decide(lost, found(ROOT.resolve("New/Other.vfproj"), "Other Game"), List.of(lost)));

        assertTrue(relink.confirmation().contains("\"Other Game\", not \"Game\""), relink.confirmation());
    }

    @Test
    void theFileTheEntryAlreadyPointsAtIsOnlyCheckedAgain() {
        RecentEntry entry = entry("Game", "Here");

        assertInstanceOf(LocateDecision.Recheck.class,
                LocateDecision.decide(entry, found(entry.path(), "Game"), List.of(entry)));
    }

    @Test
    void theSameFileInAnotherLetterCaseIsTheSameFileOnWindows() {
        assumeTrue(TestSupport.isWindows(), "Windows paths ignore case");
        RecentEntry entry = entry("Game", "Here");
        Path upper = Path.of(entry.path().toString().toUpperCase(Locale.ROOT));

        assertInstanceOf(LocateDecision.Recheck.class, LocateDecision.decide(entry, found(upper, "Game"),
                List.of(entry)));
    }

    @Test
    void aFileAnotherEntryListsIsRefused() {
        RecentEntry lost = entry("Game", "Old");
        RecentEntry other = entry("Game", "Copy");

        LocateDecision.Refuse refuse = assertInstanceOf(LocateDecision.Refuse.class,
                LocateDecision.decide(lost, found(other.path(), "Game"), List.of(lost, other)));

        assertTrue(refuse.message().contains("already in the list as \"Game\""), refuse.message());
    }

    @Test
    void aRefusedPickIsRefusedWithItsReason() {
        RecentEntry lost = entry("Game", "Old");
        Path picked = ROOT.resolve("New/Game.vfproj");

        LocateDecision.Refuse refuse = assertInstanceOf(LocateDecision.Refuse.class, LocateDecision.decide(lost,
                new PickedProject.Refused(picked, "Not valid JSON: the JSON is malformed"), List.of(lost)));

        assertEquals(picked + ": Not valid JSON: the JSON is malformed.", refuse.message());
    }

    @Test
    void aFolderWithSeveralProjectsIsRefused() {
        RecentEntry lost = entry("Game", "Old");

        assertInstanceOf(LocateDecision.Refuse.class, LocateDecision.decide(lost,
                new PickedProject.Choose(ROOT.resolve("New"), List.of()), List.of(lost)));
    }
}
