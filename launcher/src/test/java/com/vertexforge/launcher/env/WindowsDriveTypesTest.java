package com.vertexforge.launcher.env;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;

import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;

/** {@code GetDriveTypeW}'s answers as drive kinds (VK-1679, VK-1695). */
class WindowsDriveTypesTest {

    /** WinBase.h: DRIVE_UNKNOWN 0, DRIVE_NO_ROOT_DIR 1, REMOVABLE 2, FIXED 3, REMOTE 4, CDROM 5, RAMDISK 6. */
    @Test
    void eachDriveTypeHasItsKind() {
        assertEquals(DriveTypes.Kind.UNKNOWN, WindowsDriveTypes.kindOf(0));
        assertEquals(DriveTypes.Kind.UNKNOWN, WindowsDriveTypes.kindOf(1));
        assertEquals(DriveTypes.Kind.REMOVABLE, WindowsDriveTypes.kindOf(2));
        assertEquals(DriveTypes.Kind.LOCAL, WindowsDriveTypes.kindOf(3));
        assertEquals(DriveTypes.Kind.REMOTE, WindowsDriveTypes.kindOf(4));
        assertEquals(DriveTypes.Kind.OPTICAL, WindowsDriveTypes.kindOf(5));
        assertEquals(DriveTypes.Kind.RAM_DISK, WindowsDriveTypes.kindOf(6));
    }

    /** A type a later Windows adds is not known, and never taken for a fixed drive. */
    @Test
    void anUnlistedTypeIsUnknown() {
        assertEquals(DriveTypes.Kind.UNKNOWN, WindowsDriveTypes.kindOf(7));
        assertEquals(DriveTypes.Kind.UNKNOWN, WindowsDriveTypes.kindOf(-1));
        assertEquals(DriveTypes.Kind.UNKNOWN, WindowsDriveTypes.kindOf(Integer.MAX_VALUE));
    }

    @Test
    @EnabledOnOs(OS.WINDOWS)
    void theSystemDriveIsAFixedDrive() {
        Path systemRoot = Path.of(System.getenv().getOrDefault("SystemDrive", "C:") + "\\");

        assertEquals(DriveTypes.Kind.LOCAL, new WindowsDriveTypes().kindOf(systemRoot));
    }

    @Test
    void nothingIsKnownWithoutARoot() {
        assertEquals(DriveTypes.Kind.UNKNOWN, new WindowsDriveTypes().kindOf(null));
    }
}
