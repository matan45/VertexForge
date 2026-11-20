package com.vertexforge.launcher.scriptlib;

import com.vertexforge.launcher.template.LibraryManifest;

import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.SortedMap;
import java.util.TreeMap;

/**
 * The library files of one script library (VK-1691), the project's or the engine's: each file's path relative to the
 * library folder and its {@linkplain LibraryManifest#hash normalised hash}, looked up by
 * {@linkplain LibraryManifest#pathKey key} as Windows compares names.
 */
public final class LibrarySnapshot {

    /**
     * One library file.
     *
     * @param relative its exact path, '/'-separated, relative to the library folder
     * @param hash     its {@link LibraryManifest#hash}
     */
    public record Entry(String relative, String hash) {
        public Entry {
            Objects.requireNonNull(relative, "relative");
            Objects.requireNonNull(hash, "hash");
        }
    }

    /** Two files of one library that Windows would see as one. */
    public static final class DuplicatePathException extends Exception {
        DuplicatePathException(String first, String second) {
            super(first.equals(second)
                    ? "\"" + first + "\" is listed twice"
                    : "\"" + first + "\" and \"" + second + "\" differ only in case, so Windows sees them as one file");
        }
    }

    public static final LibrarySnapshot EMPTY = new LibrarySnapshot(Map.of());

    private final Map<String, Entry> byKey;

    private LibrarySnapshot(Map<String, Entry> byKey) {
        this.byKey = Collections.unmodifiableMap(byKey);
    }

    /**
     * The snapshot of {@code files}: exact relative path to hash. Only {@linkplain LibraryManifest#isLibraryFile
     * library files} may be given.
     *
     * @throws DuplicatePathException when two paths have the same key
     */
    public static LibrarySnapshot of(Map<String, String> files) throws DuplicatePathException {
        Objects.requireNonNull(files, "files");
        Map<String, Entry> byKey = new HashMap<>();
        // Sorted, so that which pair is reported does not depend on the map's order.
        for (Map.Entry<String, String> file : new TreeMap<>(files).entrySet()) {
            String relative = file.getKey();
            if (!LibraryManifest.isLibraryFile(relative)) {
                throw new IllegalArgumentException(relative + " is not a library file");
            }
            Entry earlier = byKey.putIfAbsent(LibraryManifest.pathKey(relative), new Entry(relative, file.getValue()));
            if (earlier != null) {
                throw new DuplicatePathException(earlier.relative(), relative);
            }
        }
        return new LibrarySnapshot(byKey);
    }

    /** The file with this key. */
    public Optional<Entry> get(String key) {
        return Optional.ofNullable(byKey.get(key));
    }

    /** Every file by key. */
    public Map<String, Entry> byKey() {
        return byKey;
    }

    /** Every file's exact path to its hash, sorted: what a {@code .vflib.json} lists. */
    public SortedMap<String, String> hashesByPath() {
        SortedMap<String, String> hashes = new TreeMap<>();
        byKey.values().forEach(entry -> hashes.put(entry.relative(), entry.hash()));
        return hashes;
    }

    public int size() {
        return byKey.size();
    }

    public boolean isEmpty() {
        return byKey.isEmpty();
    }
}
