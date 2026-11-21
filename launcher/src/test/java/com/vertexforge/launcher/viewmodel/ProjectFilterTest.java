package com.vertexforge.launcher.viewmodel;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.model.RecentEntry;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.List;
import java.util.UUID;
import java.util.function.Predicate;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The project list's search (VK-1694). */
class ProjectFilterTest {

    private static final Path PROJECTS = Path.of("D:/Work/Games").toAbsolutePath();

    private static ProjectViewModel row(String name, String folder, String engine) {
        RecentEntry entry = RecentEntry.create(UUID.randomUUID(), PROJECTS.resolve(folder).resolve(name + ".vfproj"),
                name, TestSupport.T0);
        ProjectViewModel row = new ProjectViewModel(entry);
        row.setEngineText(engine);
        return row;
    }

    @Test
    void aBlankQueryMatchesEveryProject() {
        ProjectViewModel game = row("Game", "Game", "1.0.0 Development");

        assertTrue(ProjectFilter.of("").test(game));
        assertTrue(ProjectFilter.of("   ").test(game));
        assertTrue(ProjectFilter.of(null).test(game));
    }

    @Test
    void theNamePathAndEngineAreSearched() {
        ProjectViewModel game = row("Space Shooter", "Arcade", "1.2.0 Release");

        assertTrue(ProjectFilter.of("shoot").test(game), "the name");
        assertTrue(ProjectFilter.of("arcade").test(game), "the path");
        assertTrue(ProjectFilter.of("release").test(game), "the engine");
        assertFalse(ProjectFilter.of("racing").test(game));
    }

    @Test
    void everyWordMustMatchSomewhere() {
        ProjectViewModel game = row("Space Shooter", "Arcade", "1.2.0 Release");

        assertTrue(ProjectFilter.of("space  arcade\t1.2").test(game));
        assertFalse(ProjectFilter.of("space racing").test(game));
    }

    @Test
    void caseIsIgnoredTheSameWayOnEveryMachine() {
        // "I" lower-cases to a dotless i in Turkish; the search must not depend on the user's locale.
        ProjectViewModel game = row("INVADERS", "Retro", "1.0.0 Development");

        assertTrue(ProjectFilter.of("invaders").test(game));
        assertTrue(ProjectFilter.of("InVaDeRs").test(game));
        assertEquals(List.of("invaders"), ProjectFilter.words(" INVADERS "));
    }

    @Test
    void everyCallGivesANewPredicate() {
        Predicate<ProjectViewModel> first = ProjectFilter.of("game");

        assertNotSame(first, ProjectFilter.of("game"));
        assertNotSame(ProjectFilter.of(""), ProjectFilter.of(""));
    }
}
