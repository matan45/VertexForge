package com.vertexforge.launcher.scriptlib;

import com.vertexforge.launcher.template.LibraryManifest;

import java.util.Objects;
import java.util.Optional;
import java.util.Set;

/**
 * What a project's library files looked like before the user touched them (VK-1691): the file the update compares the
 * project's and the engine's copies against, to tell the user's edits from the engine's changes.
 */
public sealed interface LibraryBaseline {

    /**
     * The project's {@code .vflib.json}: exactly what the engine installed, file by file.
     *
     * @param engineVersion the version of the engine whose library it records
     * @param files         the files it lists
     */
    record Recorded(String engineVersion, LibrarySnapshot files) implements LibraryBaseline {
        public Recorded {
            Objects.requireNonNull(engineVersion, "engineVersion");
            Objects.requireNonNull(files, "files");
        }

        /** The recorded hash of the file with this key. */
        public Optional<String> hash(String key) {
            return files.get(key).map(LibrarySnapshot.Entry::hash);
        }

        /** Reads a parsed {@code .vflib.json}. */
        public static Recorded of(LibraryManifest.Contents contents) throws LibrarySnapshot.DuplicatePathException {
            return new Recorded(contents.engineVersion(), LibrarySnapshot.of(contents.files()));
        }
    }

    /**
     * No usable {@code .vflib.json}: every version the launcher knows each path had.
     *
     * @param hashes the known versions
     * @param note   why there is no record, for the user: none yet (an older project), or one that cannot be read
     */
    record Legacy(LegacyLibraryHashes hashes, String note) implements LibraryBaseline {
        public Legacy {
            Objects.requireNonNull(hashes, "hashes");
            Objects.requireNonNull(note, "note");
        }

        /** The known hashes of the file with this key. */
        public Set<String> known(String key) {
            return hashes.hashes(key);
        }
    }
}
