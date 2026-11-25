package com.vertexforge.launcher.env;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Locale;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The user's profile, Desktop, Documents and Downloads as Windows reports them (VK-1695). */
class WindowsKnownFoldersTest {

    /**
     * pom.xml points {@code user.home} into target/ for the test JVM (a backstop for ~/.vertexforge), so the real
     * profile is compared through {@code USERPROFILE}, which Windows sets to the same folder FOLDERID_Profile names.
     */
    @Test
    @EnabledOnOs(OS.WINDOWS)
    void windowsNamesTheUsersFoldersAndTheProfileIsAmongThem() {
        KnownFolders system = KnownFolders.system();
        assertInstanceOf(WindowsKnownFolders.class, system);

        List<Path> folders = system.userFolders();

        assertFalse(folders.isEmpty());
        for (Path folder : folders) {
            assertTrue(folder.isAbsolute(), folder.toString());
            assertTrue(Files.isDirectory(folder), folder.toString());
        }
        assertEquals(folders.size(), folders.stream().distinct().count(), "no folder twice: " + folders);
        String profile = System.getenv("USERPROFILE");
        assertTrue(profile != null && !profile.isBlank(), "USERPROFILE is not set");
        assertTrue(folders.stream().anyMatch(folder -> folder.toString().toLowerCase(Locale.ROOT)
                        .equals(Path.of(profile).toString().toLowerCase(Locale.ROOT))),
                "the profile " + profile + " is not in " + folders);
    }

    /** The profile always exists; FOLDERID_Profile answers it on its own too. */
    @Test
    @EnabledOnOs(OS.WINDOWS)
    void theProfileIsTheFirstFolder() {
        Optional<Path> profile = WindowsKnownFolders.folderPath(WindowsKnownFolders.USER_FOLDER_IDS.getFirst());

        assertTrue(profile.isPresent());
        assertEquals(profile.get(), KnownFolders.system().userFolders().getFirst());
    }

    /** An id Windows does not know gives no folder, and no exception. */
    @Test
    @EnabledOnOs(OS.WINDOWS)
    void anUnknownFolderIdGivesNoFolder() {
        assertEquals(Optional.empty(), WindowsKnownFolders.folderPath("01234567-89ab-cdef-0123-456789abcdef"));
        assertEquals(Optional.empty(), WindowsKnownFolders.folderPath("not a guid"));
    }

    @Test
    void onlyAnAbsolutePathIsAFolder() {
        assertEquals(Optional.empty(), WindowsKnownFolders.toFolder(null));
        assertEquals(Optional.empty(), WindowsKnownFolders.toFolder(" "));
        assertEquals(Optional.empty(), WindowsKnownFolders.toFolder("relative\\folder"));
        Path absolute = Path.of(System.getProperty("java.io.tmpdir")).toAbsolutePath();
        assertEquals(Optional.of(absolute), WindowsKnownFolders.toFolder(absolute.toString()));
    }

    @Test
    void noneKnowsNoFolders() {
        assertEquals(List.of(), KnownFolders.none().userFolders());
    }
}
