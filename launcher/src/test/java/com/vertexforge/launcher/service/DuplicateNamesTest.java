package com.vertexforge.launcher.service;

import org.junit.jupiter.api.Test;

import java.util.HashSet;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;

/** The name "Duplicate…" offers (VK-1695). */
class DuplicateNamesTest {

    @Test
    void theFirstFreeCopyNameIsOffered() {
        assertEquals("Game-copy", DuplicateNames.suggest("Game", "Game", name -> false));
        Set<String> taken = Set.of("Game-copy", "Game-copy-2");
        assertEquals("Game-copy-3", DuplicateNames.suggest("Game", "Game", taken::contains));
    }

    @Test
    void whenEveryNumberIsTakenTheFirstNameIsOfferedForTheUserToChange() {
        Set<String> taken = new HashSet<>();
        taken.add("Game-copy");
        for (int number = 2; number <= DuplicateNames.MAX_NUMBER; number++) {
            taken.add("Game-copy-" + number);
        }

        assertEquals("Game-copy", DuplicateNames.suggest("Game", "Game", taken::contains));
    }

    @Test
    void aFolderNameThatMakesNoValidNameFallsBackToTheProjectsName() {
        assertEquals("Shooter-copy", DuplicateNames.suggest(" leading", "Shooter", name -> false));
        assertEquals("Project-copy", DuplicateNames.suggest(" leading", " also", name -> false));
        assertEquals("Shooter-copy", DuplicateNames.suggest(null, "Shooter", name -> false));
    }
}
