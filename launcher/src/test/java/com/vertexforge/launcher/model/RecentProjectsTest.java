package com.vertexforge.launcher.model;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Instant;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class RecentProjectsTest {

    private static final Instant T0 = Instant.parse("2026-01-01T00:00:00Z");

    @TempDir
    Path tempDir;

    private ProjectInfo project(String name, int minutesAfterT0) {
        return new ProjectInfo(name, tempDir.resolve(name + ".vfproj"), T0.plusSeconds(60L * minutesAfterT0));
    }

    private static List<String> names(RecentProjects projects) {
        return projects.getAll().stream().map(ProjectInfo::name).toList();
    }

    @Test
    void getAllReturnsNewestFirst() {
        RecentProjects projects = new RecentProjects()
                .add(project("Old", 1))
                .add(project("Newest", 3))
                .add(project("Middle", 2));

        assertEquals(List.of("Newest", "Middle", "Old"), names(projects));
        assertEquals(3, projects.size());
    }

    @Test
    void addingTheSamePathReplacesTheEntry() {
        RecentProjects projects = new RecentProjects()
                .add(project("Game", 1))
                .add(new ProjectInfo("Renamed", tempDir.resolve("Game.vfproj"), T0.plusSeconds(600)));

        assertEquals(List.of("Renamed"), names(projects));
    }

    @Test
    void exceedingTheCapDropsTheOldestEntry() {
        RecentProjects projects = new RecentProjects(3)
                .add(project("A", 5))
                .add(project("B", 1))
                .add(project("C", 3))
                .add(project("D", 4));

        assertEquals(List.of("A", "D", "C"), names(projects));
    }

    @Test
    void defaultCapIsTen() {
        RecentProjects projects = new RecentProjects();
        for (int i = 0; i < 11; i++) {
            projects.add(project("P" + i, i));
        }

        assertEquals(10, RecentProjects.DEFAULT_MAX_SIZE);
        assertEquals(10, projects.size());
        assertTrue(names(projects).stream().noneMatch("P0"::equals), "the oldest entry must be dropped");
    }

    @Test
    void touchMovesTheProjectToTheFront() {
        RecentProjects projects = new RecentProjects()
                .add(project("Old", 1))
                .add(project("New", 2));

        projects.touch(tempDir.resolve("Old.vfproj"), T0.plusSeconds(3600));

        assertEquals(List.of("Old", "New"), names(projects));
        assertEquals(T0.plusSeconds(3600), projects.getAll().getFirst().lastOpened());
    }

    @Test
    void touchOfAnUnknownPathChangesNothing() {
        RecentProjects projects = new RecentProjects().add(project("Game", 1));

        projects.touch(tempDir.resolve("Unknown.vfproj"), T0.plusSeconds(3600));

        assertEquals(List.of("Game"), names(projects));
        assertEquals(T0.plusSeconds(60), projects.getAll().getFirst().lastOpened());
    }

    @Test
    void removeDeletesTheEntry() {
        RecentProjects projects = new RecentProjects()
                .add(project("Keep", 1))
                .add(project("Drop", 2));

        projects.remove(tempDir.resolve("Drop.vfproj"));

        assertEquals(List.of("Keep"), names(projects));
    }

    @Test
    void capBelowOneIsRejected() {
        assertThrows(IllegalArgumentException.class, () -> new RecentProjects(0));
    }

    @Test
    void invalidMeansTheProjectFileIsMissing() throws IOException {
        ProjectInfo present = project("Present", 1);
        Files.writeString(present.path(), "{}");
        RecentProjects projects = new RecentProjects()
                .add(present)
                .add(project("Missing", 2));

        assertEquals(1, projects.countInvalid());

        projects.removeInvalid();

        assertEquals(List.of("Present"), names(projects));
        assertEquals(0, projects.countInvalid());
    }
}
