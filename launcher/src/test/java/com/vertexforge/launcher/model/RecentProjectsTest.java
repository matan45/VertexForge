package com.vertexforge.launcher.model;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.file.Path;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class RecentProjectsTest {

    private static final Instant T0 = Instant.parse("2026-01-01T00:00:00Z");

    @TempDir
    Path tempDir;

    private Path path(String name) {
        return tempDir.resolve(name + ".vfproj");
    }

    private RecentEntry entry(String name, int minutesAfterT0) {
        return RecentEntry.create(UUID.randomUUID(), path(name), name, T0.plusSeconds(60L * minutesAfterT0));
    }

    private static List<String> names(List<RecentEntry> entries) {
        return entries.stream().map(RecentEntry::displayName).toList();
    }

    @Test
    void displayOrderIsNewestFirstWithUnknownTimesLast() {
        RecentEntry noTime = RecentEntry.fromJson(UUID.randomUUID(),
                JsonParser.parseString("{\"name\": \"NoTime\", \"path\": \"/x/NoTime.vfproj\"}").getAsJsonObject());
        RecentProjects projects = new RecentProjects(List.of(entry("Old", 1), noTime, entry("Newest", 3),
                entry("Middle", 2)));

        assertEquals(List.of("Newest", "Middle", "Old", "NoTime"), names(projects.displayOrder()));
        assertEquals(List.of("Old", "NoTime", "Newest", "Middle"), names(projects.entries()), "file order is kept");
    }

    @Test
    void addingANewPathAppendsAnEntryWithTheGivenId() {
        UUID id = UUID.randomUUID();

        RecentProjects.Added added = RecentProjects.empty().add(path("Game"), "Game", T0, id);

        assertEquals(id, added.id());
        assertEquals(1, added.projects().size());
        assertEquals(path("Game"), added.projects().entries().getFirst().path());
    }

    @Test
    void addingAListedPathKeepsItsIdAndExtrasAndUpdatesNameAndTime() {
        JsonObject raw = new JsonObject();
        raw.addProperty("name", "Game");
        raw.addProperty("path", path("Game").toString());
        raw.addProperty("lastOpened", T0.toString());
        raw.addProperty("custom", 1);
        RecentEntry stored = RecentEntry.fromJson(UUID.randomUUID(), raw);
        RecentProjects projects = new RecentProjects(List.of(stored));

        RecentProjects.Added added = projects.add(path("Game"), "Renamed", T0.plusSeconds(600), UUID.randomUUID());

        RecentEntry updated = added.projects().entries().getFirst();
        assertEquals(stored.id(), added.id());
        assertEquals(stored.id(), updated.id());
        assertEquals("Renamed", updated.name());
        assertEquals(T0.plusSeconds(600), updated.lastOpened());
        assertEquals(1, updated.raw().get("custom").getAsInt());
        assertEquals(1, added.projects().size());
    }

    @Test
    void touchMovesTheProjectToTheFrontOfTheDisplayOrder() {
        RecentEntry old = entry("Old", 1);
        RecentProjects projects = new RecentProjects(List.of(old, entry("New", 2)));

        RecentProjects touched = projects.touch(old.id(), T0.plusSeconds(3600));

        assertEquals(List.of("Old", "New"), names(touched.displayOrder()));
        assertEquals(T0.plusSeconds(3600), touched.displayOrder().getFirst().lastOpened());
    }

    @Test
    void touchAndRemoveOfAnUnknownIdChangeNothing() {
        RecentProjects projects = new RecentProjects(List.of(entry("Game", 1)));

        assertSame(projects, projects.touch(UUID.randomUUID(), T0.plusSeconds(3600)));
        assertSame(projects, projects.remove(UUID.randomUUID()));
    }

    @Test
    void removeDeletesOnlyThatEntry() {
        RecentEntry drop = entry("Drop", 2);
        RecentProjects projects = new RecentProjects(List.of(entry("Keep", 1), drop));

        assertEquals(List.of("Keep"), names(projects.remove(drop.id()).entries()));
    }

    @Test
    void trimDropsTheOldestFirst() {
        RecentProjects projects = new RecentProjects(List.of(entry("A", 5), entry("B", 1), entry("C", 3),
                entry("D", 4)));

        assertEquals(List.of("A", "D", "C"), names(projects.trim(3, null).displayOrder()));
    }

    @Test
    void trimNeverDropsAPinnedEntryOrTheOneJustAdded() {
        RecentEntry pinned = entry("Pinned", 0).withPinned(true);
        RecentEntry justAdded = entry("JustAdded", -10);
        RecentProjects projects = new RecentProjects(List.of(pinned, justAdded, entry("Old", 1), entry("New", 2)));

        RecentProjects trimmed = projects.trim(2, justAdded.id());

        assertEquals(List.of("Pinned", "JustAdded"), names(trimmed.entries()));
        // With only protected entries left, the list may stay above the limit.
        assertEquals(2, projects.trim(1, justAdded.id()).size());
    }

    @Test
    void theDefaultLimitComfortablyHoldsFiftyProjects() {
        List<RecentEntry> fiftyOne = new ArrayList<>();
        for (int i = 0; i < 51; i++) {
            fiftyOne.add(entry("P" + i, i));
        }

        RecentProjects trimmed = new RecentProjects(fiftyOne).trim(50, null);

        assertEquals(50, trimmed.size());
        assertTrue(names(trimmed.entries()).stream().noneMatch("P0"::equals), "the oldest entry must be dropped");
    }

    @Test
    void aLimitBelowOneIsRejected() {
        assertThrows(IllegalArgumentException.class, () -> RecentProjects.empty().trim(0, null));
    }

    @Test
    void nothingHereTouchesTheFileSystem() {
        // Entries whose files do not exist are kept by every operation.
        RecentEntry missing = entry("Missing", 1);
        RecentProjects projects = new RecentProjects(List.of(missing))
                .add(path("Other"), "Other", T0, UUID.randomUUID()).projects()
                .touch(missing.id(), T0.plusSeconds(5))
                .trim(50, null);

        assertEquals(2, projects.size());
    }
}
