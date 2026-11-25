package com.vertexforge.launcher.env;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;

/**
 * {@code kernel32!GetDriveTypeW} through the Foreign Function API (VK-1679).
 * <p>
 * Looking the function up is a restricted operation: without native access the JVM prints a one-time warning, and a
 * future JDK may refuse it. pom.xml grants it: {@code --enable-native-access=com.vertexforge.launcher} for the app
 * image and {@code javafx:run}, {@code ALL-UNNAMED} for the unit tests, which run from the classpath (VK-1692).
 * Any failure, now or later, only makes every drive {@link DriveTypes.Kind#UNKNOWN}. The lookup happens on first use,
 * never at startup.
 */
final class WindowsDriveTypes implements DriveTypes {

    /**
     * {@code GetDriveTypeW}'s answers (WinBase.h). 0 ({@code DRIVE_UNKNOWN}) and 1 ({@code DRIVE_NO_ROOT_DIR}) say
     * nothing about the drive and fall into {@link Kind#UNKNOWN} with everything else.
     */
    private static final int DRIVE_REMOVABLE = 2;
    private static final int DRIVE_FIXED = 3;
    private static final int DRIVE_REMOTE = 4;
    private static final int DRIVE_CDROM = 5;
    private static final int DRIVE_RAMDISK = 6;

    /** The function, or {@code null} when it could not be looked up. Initialized on first use. */
    private static final class Holder {
        static final MethodHandle GET_DRIVE_TYPE = lookup();

        private static MethodHandle lookup() {
            try {
                MemorySegment address = SymbolLookup.libraryLookup("kernel32", Arena.global())
                        .find("GetDriveTypeW")
                        .orElse(null);
                if (address == null) {
                    return null;
                }
                return Linker.nativeLinker().downcallHandle(address,
                        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
            } catch (Throwable t) {
                return null;
            }
        }
    }

    @Override
    public Kind kindOf(Path root) {
        if (root == null) {
            return Kind.UNKNOWN;
        }
        try {
            MethodHandle getDriveType = Holder.GET_DRIVE_TYPE;
            if (getDriveType == null) {
                return Kind.UNKNOWN;
            }
            // The root must end in a backslash: "C:\" or "\\server\share\".
            String text = root.toString();
            if (!text.endsWith("\\")) {
                text += "\\";
            }
            int type;
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment name = arena.allocateFrom(text, StandardCharsets.UTF_16LE);
                type = (int) getDriveType.invokeExact(name);
            }
            return kindOf(type);
        } catch (Throwable t) {
            return Kind.UNKNOWN;
        }
    }

    /**
     * The kind a {@code GetDriveTypeW} answer names. Only a fixed drive is {@link Kind#LOCAL}; anything this list does
     * not name (0, 1, or a value a later Windows adds) is {@link Kind#UNKNOWN}, never a guess.
     */
    static Kind kindOf(int driveType) {
        return switch (driveType) {
            case DRIVE_REMOVABLE -> Kind.REMOVABLE;
            case DRIVE_FIXED -> Kind.LOCAL;
            case DRIVE_REMOTE -> Kind.REMOTE;
            case DRIVE_CDROM -> Kind.OPTICAL;
            case DRIVE_RAMDISK -> Kind.RAM_DISK;
            default -> Kind.UNKNOWN;
        };
    }
}
