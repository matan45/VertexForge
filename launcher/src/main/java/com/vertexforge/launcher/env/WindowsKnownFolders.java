package com.vertexforge.launcher.env;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.nio.charset.StandardCharsets;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;

/**
 * {@code shell32!SHGetKnownFolderPath} through the Foreign Function API (VK-1695), for {@link KnownFolders}.
 * <p>
 * It answers where each folder really is: redirected by OneDrive's folder backup, moved by the user in the folder's
 * Location tab, or set by policy. With flags 0 Windows checks that the folder exists and fails when it does not, so
 * every answer is a folder that was there a moment ago. The returned string is the shell's to allocate and ours to
 * free with {@code ole32!CoTaskMemFree}, which it asks for even after a failure. Native access is granted as for
 * {@link WindowsDriveTypes}; any failure leaves that folder out, and a failed lookup gives an empty list.
 */
final class WindowsKnownFolders implements KnownFolders {

    /**
     * FOLDERID_Profile, FOLDERID_Desktop, FOLDERID_Documents, FOLDERID_Downloads, then FOLDERID_Pictures,
     * FOLDERID_Music, FOLDERID_Videos and FOLDERID_SavedGames (KnownFolders.h, SDK 10.0.26100): the profile first.
     */
    static final List<String> USER_FOLDER_IDS = List.of(
            "5e6c858f-0e22-4760-9afe-ea3317b67173",
            "b4bfcc3a-db2c-424c-b029-7fe99a87c641",
            "fdd39ad0-238f-46af-adb4-6c85480369c7",
            "374de290-123f-4565-9164-39c4925e467b",
            "33e28130-4e1e-4676-835a-98395c3bc3bb",
            "4bd8d571-6d19-48d3-be97-422220080e43",
            "18989b1d-99b5-455b-841c-ab7c74e4ddfc",
            "4c5c32ff-bb9d-43b0-b5b4-2d72e54eaaa4");

    /** {@code KF_FLAG_DEFAULT}: verify the folder exists, create nothing. */
    private static final int KF_FLAG_DEFAULT = 0;
    /** The longest path Windows has: 32767 UTF-16 units and the terminator. */
    private static final long MAX_PATH_BYTES = (32767 + 1) * 2L;

    /** The functions, or {@code null} when either could not be looked up. Initialized on first use. */
    private static final class Holder {
        static final MethodHandle GET_KNOWN_FOLDER_PATH;
        static final MethodHandle CO_TASK_MEM_FREE;

        static {
            MethodHandle get = null;
            MethodHandle free = null;
            try {
                Linker linker = Linker.nativeLinker();
                // HRESULT SHGetKnownFolderPath(REFKNOWNFOLDERID rfid, DWORD dwFlags, HANDLE hToken, PWSTR *ppszPath)
                get = linker.downcallHandle(SymbolLookup.libraryLookup("shell32", Arena.global())
                                .find("SHGetKnownFolderPath").orElseThrow(),
                        FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
                                ValueLayout.ADDRESS, ValueLayout.ADDRESS));
                // void CoTaskMemFree(LPVOID pv)
                free = linker.downcallHandle(SymbolLookup.libraryLookup("ole32", Arena.global())
                                .find("CoTaskMemFree").orElseThrow(),
                        FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));
            } catch (Throwable t) {
                get = null;
                free = null;
            }
            GET_KNOWN_FOLDER_PATH = get;
            CO_TASK_MEM_FREE = free;
        }
    }

    @Override
    public List<Path> userFolders() {
        try {
            if (Holder.GET_KNOWN_FOLDER_PATH == null || Holder.CO_TASK_MEM_FREE == null) {
                return List.of();
            }
            Set<Path> folders = new LinkedHashSet<>();
            for (String id : USER_FOLDER_IDS) {
                folderPath(id).ifPresent(folders::add);
            }
            return List.copyOf(folders);
        } catch (Throwable t) {
            return List.of();
        }
    }

    /** Where the known folder {@code id} is; empty when Windows does not say, or says something that is no path. */
    static Optional<Path> folderPath(String id) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(ValueLayout.ADDRESS);
            out.set(ValueLayout.ADDRESS, 0, MemorySegment.NULL);
            int hr = (int) Holder.GET_KNOWN_FOLDER_PATH.invokeExact(Guid.of(id, arena), KF_FLAG_DEFAULT,
                    MemorySegment.NULL, out);
            MemorySegment text = out.get(ValueLayout.ADDRESS, 0);
            try {
                if (hr != ShellResults.S_OK || text.address() == 0) {
                    return Optional.empty();
                }
                return toFolder(text.reinterpret(MAX_PATH_BYTES).getString(0, StandardCharsets.UTF_16LE));
            } finally {
                if (text.address() != 0) {
                    Holder.CO_TASK_MEM_FREE.invokeExact(text);
                }
            }
        } catch (Throwable t) {
            return Optional.empty();
        }
    }

    /** An absolute path, or empty for an empty, relative or unparseable one. */
    static Optional<Path> toFolder(String text) {
        if (text == null || text.isBlank()) {
            return Optional.empty();
        }
        try {
            Path path = Path.of(text);
            return path.isAbsolute() ? Optional.of(path) : Optional.empty();
        } catch (InvalidPathException e) {
            return Optional.empty();
        }
    }
}
