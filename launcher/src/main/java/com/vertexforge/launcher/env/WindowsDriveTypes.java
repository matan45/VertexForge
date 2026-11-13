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
 * Looking the function up is a restricted operation: without {@code --enable-native-access=ALL-UNNAMED} (set in
 * pom.xml for the jar, the app image and the tests) the JVM prints a one-time warning, and a future JDK may refuse it.
 * Any failure, now or later, only makes every drive {@link DriveTypes.Kind#UNKNOWN}. The lookup happens on first use,
 * never at startup.
 */
final class WindowsDriveTypes implements DriveTypes {

    private static final int DRIVE_UNKNOWN = 0;
    private static final int DRIVE_NO_ROOT_DIR = 1;
    private static final int DRIVE_REMOTE = 4;

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
            return switch (type) {
                case DRIVE_REMOTE -> Kind.REMOTE;
                case DRIVE_UNKNOWN, DRIVE_NO_ROOT_DIR -> Kind.UNKNOWN;
                default -> Kind.LOCAL;
            };
        } catch (Throwable t) {
            return Kind.UNKNOWN;
        }
    }
}
