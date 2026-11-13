package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.env.DriveTypes;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

/** The paths here are Windows paths, which only parse as intended on Windows. */
class LocationWarningsTest {

    private static final Charset WINDOWS_1252 = Charset.forName("windows-1252");

    @BeforeEach
    void onWindows() {
        assumeTrue(TestSupport.isWindows(), "Windows paths, drives and OneDrive");
    }

    private static LocationWarnings warnings(Map<String, String> env, Charset charset, DriveTypes drives) {
        return new LocationWarnings(env::get, charset, drives);
    }

    private static List<String> warningsFor(LocationWarnings warnings, String location) {
        Path folder = Path.of(location);
        return warnings.warningsFor(folder, folder.resolve("Game").resolve("Game.vfproj"));
    }

    @Test
    void aFolderInsideAOneDriveSyncFolderWarns() {
        LocationWarnings warnings = warnings(Map.of("OneDrive", "C:\\Users\\me\\OneDrive"), StandardCharsets.UTF_8,
                DriveTypes.unknown());

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
                "OneDriveCommercial", "D:\\Work\\OneDrive - Contoso"), StandardCharsets.UTF_8, DriveTypes.unknown());

        assertTrue(warnings.underOneDrive(Path.of("C:\\Users\\me\\OneDrive\\Games")));
        assertTrue(warnings.underOneDrive(Path.of("D:\\Work\\OneDrive - Contoso\\Games")));
        assertFalse(warnings.underOneDrive(Path.of("D:\\Work\\Games")));
    }

    @Test
    void aBlankOrRelativeOneDriveVariableIsIgnored() {
        LocationWarnings warnings = warnings(Map.of("OneDrive", "  ", "OneDriveConsumer", "OneDrive"),
                StandardCharsets.UTF_8, DriveTypes.unknown());

        assertFalse(warnings.underOneDrive(Path.of("C:\\OneDrive\\Games")));
    }

    @Test
    void uncPathsAndRemoteDrivesWarn() {
        DriveTypes zIsRemote = root -> root.toString().equalsIgnoreCase("Z:\\")
                ? DriveTypes.Kind.REMOTE : DriveTypes.Kind.LOCAL;
        LocationWarnings warnings = warnings(Map.of(), StandardCharsets.UTF_8, zIsRemote);

        assertEquals(List.of(LocationWarnings.NETWORK_WARNING), warningsFor(warnings, "\\\\server\\share\\Projects"));
        assertEquals(List.of(LocationWarnings.NETWORK_WARNING), warningsFor(warnings, "Z:\\Projects"));
        assertEquals(List.of(), warningsFor(warnings, "C:\\Projects"));
    }

    @Test
    void notKnowingTheDriveTypeNeverWarns() {
        DriveTypes broken = root -> {
            throw new IllegalStateException("no native access");
        };

        assertEquals(List.of(), warningsFor(warnings(Map.of(), StandardCharsets.UTF_8, broken), "Z:\\Projects"));
        assertEquals(List.of(), warningsFor(warnings(Map.of(), StandardCharsets.UTF_8, DriveTypes.unknown()),
                "Z:\\Projects"));
    }

    @Test
    void charactersOutsideTheCodePageWarnAndNameThemselves() {
        LocationWarnings ansi = warnings(Map.of(), WINDOWS_1252, DriveTypes.unknown());

        List<String> hebrew = warningsFor(ansi, "C:\\\u05e4\u05e8\u05d5\u05d9\u05e7\u05d8\u05d9\u05dd");
        assertEquals(1, hebrew.size());
        assertTrue(hebrew.getFirst().contains("\"\u05e4\", \"\u05e8\""), hebrew.getFirst());
        assertTrue(hebrew.getFirst().contains("(windows-1252)"), hebrew.getFirst());
        // é is in windows-1252.
        assertEquals(List.of(), warningsFor(ansi, "C:\\Projets\\Caf\u00e9"));
        assertEquals(Optional.empty(), ansi.unencodable("C:\\Game\\Game.vfproj"));
    }

    @Test
    void aUtf8CodePageNeverWarns() {
        LocationWarnings utf8 = warnings(Map.of(), StandardCharsets.UTF_8, DriveTypes.unknown());

        assertEquals(List.of(), warningsFor(utf8, "C:\\\u05e4\u05e8\u05d5\u05d9\u05e7\u05d8\u05d9\u05dd\\\u6e38\u620f"));
    }

    @Test
    void theNameCountsTooNotOnlyTheFolder() {
        LocationWarnings ansi = warnings(Map.of(), WINDOWS_1252, DriveTypes.unknown());
        Path folder = Path.of("C:\\Projects");
        String name = "\u6e38\u620f";

        List<String> found = ansi.warningsFor(folder, folder.resolve(name).resolve(name + ".vfproj"));

        assertEquals(1, found.size());
    }

    @Test
    void theSystemDriveIsLocal() {
        Path systemRoot = Path.of(System.getenv().getOrDefault("SystemDrive", "C:") + "\\");

        assertEquals(DriveTypes.Kind.LOCAL, DriveTypes.system().kindOf(systemRoot));
    }
}
