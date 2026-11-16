package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.env.DriveTypes;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

/** The paths here are Windows paths, which only parse as intended on Windows. */
class LocationWarningsTest {

    @BeforeEach
    void onWindows() {
        assumeTrue(TestSupport.isWindows(), "Windows paths, drives and OneDrive");
    }

    private static LocationWarnings warnings(Map<String, String> env, DriveTypes drives) {
        return new LocationWarnings(env::get, drives);
    }

    private static List<String> warningsFor(LocationWarnings warnings, String location) {
        return warnings.warningsFor(Path.of(location));
    }

    @Test
    void aFolderInsideAOneDriveSyncFolderWarns() {
        LocationWarnings warnings = warnings(Map.of("OneDrive", "C:\\Users\\me\\OneDrive"), DriveTypes.unknown());

        assertEquals(List.of(LocationWarnings.ONEDRIVE_WARNING), warningsFor(warnings, "C:\\Users\\me\\OneDrive"));
        assertEquals(List.of(LocationWarnings.ONEDRIVE_WARNING),
                warningsFor(warnings, "c:\\users\\ME\\onedrive\\Documents\\Projects"));
        assertEquals(List.of(), warningsFor(warnings, "C:\\Users\\me\\OneDriveBackup\\Projects"));
        assertEquals(List.of(), warningsFor(warnings, "C:\\Projects"));
    }

    @Test
    void everyOneDriveAccountCounts() {
        LocationWarnings warnings = warnings(Map.of(
                "OneDriveConsumer", "C:\\Users\\me\\OneDrive",
                "OneDriveCommercial", "D:\\Work\\OneDrive - Contoso"), DriveTypes.unknown());

        assertTrue(warnings.underOneDrive(Path.of("C:\\Users\\me\\OneDrive\\Games")));
        assertTrue(warnings.underOneDrive(Path.of("D:\\Work\\OneDrive - Contoso\\Games")));
        assertFalse(warnings.underOneDrive(Path.of("D:\\Work\\Games")));
    }

    @Test
    void aBlankOrRelativeOneDriveVariableIsIgnored() {
        LocationWarnings warnings = warnings(Map.of("OneDrive", "  ", "OneDriveConsumer", "OneDrive"),
                DriveTypes.unknown());

        assertFalse(warnings.underOneDrive(Path.of("C:\\OneDrive\\Games")));
    }

    @Test
    void uncPathsAndRemoteDrivesWarn() {
        DriveTypes zIsRemote = root -> root.toString().equalsIgnoreCase("Z:\\")
                ? DriveTypes.Kind.REMOTE : DriveTypes.Kind.LOCAL;
        LocationWarnings warnings = warnings(Map.of(), zIsRemote);

        assertEquals(List.of(LocationWarnings.NETWORK_WARNING), warningsFor(warnings, "\\\\server\\share\\Projects"));
        assertEquals(List.of(LocationWarnings.NETWORK_WARNING), warningsFor(warnings, "Z:\\Projects"));
        assertEquals(List.of(), warningsFor(warnings, "C:\\Projects"));
    }

    @Test
    void notKnowingTheDriveTypeNeverWarns() {
        DriveTypes broken = root -> {
            throw new IllegalStateException("no native access");
        };

        assertEquals(List.of(), warningsFor(warnings(Map.of(), broken), "Z:\\Projects"));
        assertEquals(List.of(), warningsFor(warnings(Map.of(), DriveTypes.unknown()), "Z:\\Projects"));
    }

    /** The editor runs in the UTF-8 code page (VK-1684), so no character in the path is a reason to warn. */
    @Test
    void charactersOutsideTheAnsiCodePageNeverWarn() {
        LocationWarnings warnings = warnings(Map.of(), DriveTypes.unknown());

        assertEquals(List.of(), warningsFor(warnings, "C:\\\u05e4\u05e8\u05d5\u05d9\u05e7\u05d8\u05d9\u05dd\\\u6e38\u620f"));
    }

    @Test
    void theSystemDriveIsLocal() {
        Path systemRoot = Path.of(System.getenv().getOrDefault("SystemDrive", "C:") + "\\");

        assertEquals(DriveTypes.Kind.LOCAL, DriveTypes.system().kindOf(systemRoot));
    }
}
