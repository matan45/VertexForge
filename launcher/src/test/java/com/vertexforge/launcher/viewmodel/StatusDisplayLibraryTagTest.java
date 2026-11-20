package com.vertexforge.launcher.viewmodel;

import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.service.ScriptLibraryStatusService.LibraryState;
import com.vertexforge.launcher.session.EditorSession;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.time.Instant;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The Status column's script-library tag (VK-1691). */
class StatusDisplayLibraryTagTest {

    @Test
    void anOkProjectShowsItsLibrarysTag() {
        assertEquals("Library outdated", new StatusDisplay(ProjectStatus.ok(), null, LibraryState.OUTDATED)
                .libraryTag());
        assertEquals("Library outdated", new StatusDisplay(ProjectStatus.ok(), null, LibraryState.LEGACY)
                .libraryTag());
        assertEquals("Library missing", new StatusDisplay(ProjectStatus.ok(), null, LibraryState.MISSING)
                .libraryTag());
        assertEquals("Library update interrupted", new StatusDisplay(ProjectStatus.ok(), null,
                LibraryState.INTERRUPTED).libraryTag());
        assertEquals("", new StatusDisplay(ProjectStatus.ok(), null, LibraryState.UP_TO_DATE).libraryTag());
        assertEquals("", new StatusDisplay(ProjectStatus.ok(), null, LibraryState.UNKNOWN).libraryTag());
    }

    @Test
    void aProjectThatIsNotOkShowsNoTag() {
        for (ProjectStatus status : new ProjectStatus[]{ProjectStatus.CHECKING, ProjectStatus.missing("gone"),
                ProjectStatus.invalid("bad"), ProjectStatus.unreadable("denied"), ProjectStatus.alreadyOpen("open"),
                ProjectStatus.needsNewerEngine("Needs VertexForge 2.0.0", "newer")}) {
            StatusDisplay display = new StatusDisplay(status, null, LibraryState.OUTDATED);
            assertEquals("", display.libraryTag(), status.toString());
            assertEquals(status.reason(), display.tooltip(), "the file's reason alone");
        }
    }

    @Test
    void aRunningEditorHidesTheTag() {
        for (EditorSession.Phase phase : new EditorSession.Phase[]{EditorSession.Phase.STARTING,
                EditorSession.Phase.LOADING, EditorSession.Phase.READY, EditorSession.Phase.NO_READY_SIGNAL}) {
            StatusDisplay display = new StatusDisplay(ProjectStatus.ok(), phase, LibraryState.OUTDATED);
            assertEquals("", display.libraryTag(), phase.name());
            assertFalse(display.tooltip().contains("Update Script Library"), display.tooltip());
        }
        assertEquals("Library outdated", new StatusDisplay(ProjectStatus.ok(), EditorSession.Phase.EXITED,
                LibraryState.OUTDATED).libraryTag(), "an editor that exited runs no more");
    }

    @Test
    void theTooltipAddsTheLibrarysExplanationToTheFilesReason() {
        StatusDisplay withReason = new StatusDisplay(ProjectStatus.ok(), null, LibraryState.OUTDATED);
        assertEquals(ProjectStatus.ok().reason() + "\n" + LibraryState.OUTDATED.tooltip(), withReason.tooltip());

        StatusDisplay withoutReason = new StatusDisplay(ProjectStatus.ok(""), null, LibraryState.MISSING);
        assertEquals(LibraryState.MISSING.tooltip(), withoutReason.tooltip());

        StatusDisplay quiet = new StatusDisplay(ProjectStatus.ok(), null, LibraryState.UP_TO_DATE);
        assertEquals(ProjectStatus.ok().reason(), quiet.tooltip());
    }

    @Test
    void aMissingLibraryStateIsUnknown() {
        assertEquals(LibraryState.UNKNOWN, new StatusDisplay(ProjectStatus.ok(), null, null).library());
        assertEquals(LibraryState.UNKNOWN, new StatusDisplay(ProjectStatus.ok(), null).library());
    }

    @Test
    void theRowsLibraryStateFeedsItsStatusDisplay() {
        ProjectViewModel row = new ProjectViewModel(RecentEntry.create(UUID.randomUUID(),
                Path.of("C:\\projects\\Game\\Game.vfproj"), "Game", Instant.EPOCH));
        row.setStatus(ProjectStatus.ok());
        assertEquals(LibraryState.UNKNOWN, row.getLibrary());
        assertEquals("", row.getStatusDisplay().libraryTag());

        row.setLibrary(LibraryState.LEGACY);
        assertEquals("Library outdated", row.getStatusDisplay().libraryTag());

        row.setLibrary(null);
        assertEquals(LibraryState.UNKNOWN, row.getLibrary());
        assertEquals("", row.getStatusDisplay().libraryTag());
    }

    @Test
    void onlyTheLatestLibraryCheckCounts() {
        ProjectViewModel row = new ProjectViewModel(RecentEntry.create(UUID.randomUUID(),
                Path.of("C:\\projects\\Game\\Game.vfproj"), "Game", Instant.EPOCH));

        long first = row.startLibraryCheck();
        long second = row.startLibraryCheck();

        assertFalse(row.isCurrentLibraryCheck(first));
        assertTrue(row.isCurrentLibraryCheck(second));
    }
}
