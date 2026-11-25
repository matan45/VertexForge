package com.vertexforge.launcher.scriptlib;

import com.google.gson.Strictness;
import com.google.gson.stream.JsonReader;
import com.google.gson.stream.JsonToken;
import com.vertexforge.launcher.template.LibraryManifest;

import java.io.IOException;
import java.io.InputStream;
import java.io.StringReader;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.SortedMap;
import java.util.SortedSet;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.regex.Pattern;

/**
 * The baseline of a project made before libraries were tracked (VK-1691): every version of every library file that a
 * launcher ever installed, or that the engine's {@code assets/scripts/lib} ever held, up to the commit that brought in
 * {@code .vflib.json} (VK-1689). A file whose hash is one of its path's known versions was never changed by the user.
 * <p>
 * The set is frozen: every project made since carries its own {@code .vflib.json}. It is generated from the git history
 * by the test-scope {@code LegacyLibraryHashesGenerator}, which a test compares it with.
 * <pre>
 * { "formatVersion": 1, "hashAlgorithm": "SHA-256", "lineEndings": "lf", "through": "&lt;40 hex&gt;",
 *   "files": { "core/Array.mt": ["9f86d0…", "…"], "lib/engine/Log.mt": ["…"] } }
 * </pre>
 * Paths are as installed below {@code scripts/lib}: the first launcher put its library one folder too deep, so those
 * files are listed under {@code lib/…}.
 */
public final class LegacyLibraryHashes {

    /** The resource, on the class path. */
    public static final String RESOURCE = "/scriptlib/legacy-library-hashes.json";
    public static final int FORMAT_VERSION = 1;

    private static final Pattern HASH_PATTERN = Pattern.compile("[0-9a-f]{64}");
    private static final Pattern COMMIT_PATTERN = Pattern.compile("[0-9a-f]{40}");

    private static volatile LegacyLibraryHashes bundled;

    private final String through;
    private final SortedMap<String, SortedSet<String>> files;
    private final Map<String, SortedSet<String>> byKey;

    private LegacyLibraryHashes(String through, SortedMap<String, SortedSet<String>> files) {
        this.through = through;
        this.files = Collections.unmodifiableSortedMap(files);
        Map<String, SortedSet<String>> keyed = new HashMap<>();
        files.forEach((path, hashes) -> keyed.put(LibraryManifest.pathKey(path), hashes));
        this.byKey = Collections.unmodifiableMap(keyed);
    }

    /**
     * The set this launcher carries ({@link #RESOURCE}), read once.
     *
     * @throws UncheckedIOException  when the resource is missing (a broken build)
     * @throws IllegalStateException when it is not a valid set (a broken build)
     */
    public static LegacyLibraryHashes bundled() {
        LegacyLibraryHashes hashes = bundled;
        if (hashes == null) {
            synchronized (LegacyLibraryHashes.class) {
                hashes = bundled;
                if (hashes == null) {
                    hashes = readResource();
                    bundled = hashes;
                }
            }
        }
        return hashes;
    }

    private static LegacyLibraryHashes readResource() {
        try (InputStream in = LegacyLibraryHashes.class.getResourceAsStream(RESOURCE)) {
            if (in == null) {
                throw new UncheckedIOException(new IOException("The launcher has no " + RESOURCE));
            }
            return parse(new String(in.readAllBytes(), StandardCharsets.UTF_8));
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    /**
     * Reads a set, strictly: {@code formatVersion} 1, {@code hashAlgorithm "SHA-256"}, {@code lineEndings "lf"}, a
     * 40-hex {@code through} commit, and {@code files}: each a {@linkplain LibraryManifest#isValidPath valid path}, no
     * two the same {@linkplain LibraryManifest#pathKey ignoring case}, to a non-empty array of distinct 64-hex hashes.
     *
     * @throws IllegalStateException when it is not such a set
     */
    public static LegacyLibraryHashes parse(String json) {
        Objects.requireNonNull(json, "json");
        String formatVersion = null;
        String hashAlgorithm = null;
        String lineEndings = null;
        String through = null;
        SortedMap<String, SortedSet<String>> files = null;
        try (JsonReader reader = new JsonReader(new StringReader(json))) {
            reader.setStrictness(Strictness.STRICT);
            reader.beginObject();
            Set<String> names = new HashSet<>();
            while (reader.hasNext()) {
                String name = reader.nextName();
                if (!names.add(name)) {
                    throw new IllegalStateException("\"" + name + "\" appears twice");
                }
                switch (name) {
                    case "formatVersion" -> formatVersion = reader.nextString();
                    case "hashAlgorithm" -> hashAlgorithm = reader.nextString();
                    case "lineEndings" -> lineEndings = reader.nextString();
                    case "through" -> through = reader.nextString();
                    case "files" -> files = readFiles(reader);
                    default -> throw new IllegalStateException("unknown member \"" + name + "\"");
                }
            }
            reader.endObject();
            if (reader.peek() != JsonToken.END_DOCUMENT) {
                throw new IllegalStateException("there is more data after the JSON object");
            }
        } catch (IOException e) {
            throw new IllegalStateException("it is not valid JSON: " + e.getMessage(), e);
        }

        if (!String.valueOf(FORMAT_VERSION).equals(formatVersion)) {
            throw new IllegalStateException("\"formatVersion\" is not " + FORMAT_VERSION);
        }
        if (!LibraryManifest.HASH_ALGORITHM.equals(hashAlgorithm)
                || !LibraryManifest.LINE_ENDINGS.equals(lineEndings)) {
            throw new IllegalStateException("it is not a set of " + LibraryManifest.HASH_ALGORITHM + " hashes over "
                    + LibraryManifest.LINE_ENDINGS + " line endings");
        }
        if (through == null || !COMMIT_PATTERN.matcher(through).matches()) {
            throw new IllegalStateException("\"through\" is not a 40-digit commit");
        }
        if (files == null) {
            throw new IllegalStateException("it has no \"files\"");
        }
        return new LegacyLibraryHashes(through, files);
    }

    private static SortedMap<String, SortedSet<String>> readFiles(JsonReader reader) throws IOException {
        SortedMap<String, SortedSet<String>> files = new TreeMap<>();
        Map<String, String> byKey = new HashMap<>();
        reader.beginObject();
        while (reader.hasNext()) {
            String path = reader.nextName();
            if (!LibraryManifest.isValidPath(path) || !LibraryManifest.isLibraryFile(path)) {
                throw new IllegalStateException("\"" + path + "\" is not a library path");
            }
            String earlier = byKey.putIfAbsent(LibraryManifest.pathKey(path), path);
            if (earlier != null) {
                throw new IllegalStateException("\"" + earlier + "\" and \"" + path + "\" are the same path");
            }
            SortedSet<String> hashes = new TreeSet<>();
            reader.beginArray();
            while (reader.hasNext()) {
                String hash = reader.nextString();
                if (!HASH_PATTERN.matcher(hash).matches() || !hashes.add(hash)) {
                    throw new IllegalStateException("\"" + path + "\" has an invalid or repeated hash");
                }
            }
            reader.endArray();
            if (hashes.isEmpty()) {
                throw new IllegalStateException("\"" + path + "\" has no hashes");
            }
            files.put(path, Collections.unmodifiableSortedSet(hashes));
        }
        reader.endObject();
        return files;
    }

    /** The commit the set was taken up to. */
    public String through() {
        return through;
    }

    /** Every path, as installed, to its known hashes. */
    public SortedMap<String, SortedSet<String>> files() {
        return files;
    }

    /** The known hashes of the path with this {@linkplain LibraryManifest#pathKey key}; empty when none. */
    public Set<String> hashes(String key) {
        Objects.requireNonNull(key, "key");
        SortedSet<String> hashes = byKey.get(key);
        return hashes == null ? Set.of() : hashes;
    }

    /** How many (path, hash) pairs the set holds. */
    public int pairCount() {
        return files.values().stream().mapToInt(Set::size).sum();
    }
}
