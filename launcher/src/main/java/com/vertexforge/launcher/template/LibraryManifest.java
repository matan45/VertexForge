package com.vertexforge.launcher.template;

import com.google.gson.GsonBuilder;
import com.google.gson.JsonObject;
import com.google.gson.Strictness;
import com.google.gson.stream.JsonReader;
import com.google.gson.stream.JsonToken;
import com.vertexforge.launcher.util.VersionNumber;

import java.io.IOException;
import java.io.StringReader;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.HexFormat;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.SortedMap;
import java.util.TreeMap;
import java.util.regex.Pattern;

/**
 * {@code scripts/lib/.vflib.json} (VK-1689): which engine's script library a project was created with, and a hash of
 * every library file as it was written. It is the baseline the script-library update (VK-1691) compares a project's
 * library against, to tell the user's edits from the engine's changes.
 * <pre>
 * { "formatVersion": 1, "engineVersion": "1.0.0", "hashAlgorithm": "SHA-256", "lineEndings": "lf",
 *   "files": { "core/Array.mt": "9f86d0…", "engine/Log.mt": "…" } }
 * </pre>
 * <ul>
 *   <li>{@code files}: every library file, '/'-separated and relative to the library folder, sorted by
 *       {@link String#compareTo}; never {@code .vflib.json} itself, and never the {@code .vfmeta} files the editor
 *       adds beside the scripts on the first open.</li>
 *   <li>Each hash is the lowercase hex SHA-256 of the file with every CRLF replaced by LF ({@code lineEndings: "lf"}),
 *       so that a checkout's line endings never read as an edit.</li>
 * </ul>
 * The script-library update (VK-1691) reads it back with {@link #parse} and writes a new one after every update.
 */
public final class LibraryManifest {

    public static final String FILE_NAME = ".vflib.json";
    public static final int FORMAT_VERSION = 1;
    public static final String HASH_ALGORITHM = "SHA-256";
    public static final String LINE_ENDINGS = "lf";
    /** The editor's asset sidecars ({@code Foo.mt.vfmeta}), which are never library files. */
    public static final String SIDECAR_EXTENSION = ".vfmeta";

    private static final String MEMBER_FORMAT_VERSION = "formatVersion";
    private static final String MEMBER_ENGINE_VERSION = "engineVersion";
    private static final String MEMBER_HASH_ALGORITHM = "hashAlgorithm";
    private static final String MEMBER_LINE_ENDINGS = "lineEndings";
    private static final String MEMBER_FILES = "files";
    private static final char BYTE_ORDER_MARK = '﻿';
    private static final Pattern HASH_PATTERN = Pattern.compile("[0-9a-f]{64}");
    private static final Pattern WHOLE_NUMBER = Pattern.compile("[1-9][0-9]{0,8}");

    /**
     * A {@code .vflib.json} as read back.
     *
     * @param engineVersion the {@code MAJOR.MINOR.PATCH} of the engine whose library it records
     * @param files         every library file it lists ({@link #isLibraryFile}), '/'-relative, to its {@link #hash}
     */
    public record Contents(String engineVersion, SortedMap<String, String> files) {
        public Contents {
            Objects.requireNonNull(engineVersion, "engineVersion");
            files = Collections.unmodifiableSortedMap(new TreeMap<>(files));
        }
    }

    /** Why a {@code .vflib.json} cannot be used; {@link #newerFormat()} when a newer launcher wrote it. */
    public static final class InvalidLibraryManifestException extends Exception {
        private final boolean newerFormat;

        InvalidLibraryManifestException(String message, boolean newerFormat) {
            super(message);
            this.newerFormat = newerFormat;
        }

        /** Whether the file has a newer {@code formatVersion} than this launcher reads. */
        public boolean newerFormat() {
            return newerFormat;
        }
    }

    private LibraryManifest() {
        // Utility class - prevent instantiation
    }

    /**
     * Whether the file at {@code relative} ('/'-separated, relative to the library folder) is a library file: neither
     * the library's own {@link #FILE_NAME} nor an editor sidecar ({@link #SIDECAR_EXTENSION}), both matched ignoring
     * case, as Windows names them.
     */
    public static boolean isLibraryFile(String relative) {
        Objects.requireNonNull(relative, "relative");
        return !relative.equalsIgnoreCase(FILE_NAME) && !isSidecar(relative);
    }

    /** Whether {@code relative} names an editor sidecar ({@code *.vfmeta}, ignoring case). */
    public static boolean isSidecar(String relative) {
        Objects.requireNonNull(relative, "relative");
        int length = SIDECAR_EXTENSION.length();
        return relative.length() > length
                && relative.regionMatches(true, relative.length() - length, SIDECAR_EXTENSION, 0, length);
    }

    /**
     * The form two library paths are compared in, as Windows compares names: every character upper-cased on its own
     * ({@link Character#toUpperCase(char)}, which never changes the length, unlike {@link String#toUpperCase}).
     */
    public static String pathKey(String relative) {
        Objects.requireNonNull(relative, "relative");
        char[] chars = relative.toCharArray();
        for (int i = 0; i < chars.length; i++) {
            chars[i] = Character.toUpperCase(chars[i]);
        }
        return new String(chars);
    }

    /**
     * Whether {@code relative} is a path this file may list: '/'-separated, relative, without an empty, {@code .} or
     * {@code ..} name, a backslash or a colon (a drive or an alternate data stream).
     */
    public static boolean isValidPath(String relative) {
        if (relative == null || relative.isEmpty() || relative.indexOf('\\') >= 0 || relative.indexOf(':') >= 0
                || relative.charAt(0) == '/') {
            return false;
        }
        for (String name : relative.split("/", -1)) {
            if (name.isEmpty() || name.equals(".") || name.equals("..")) {
                return false;
            }
        }
        return true;
    }

    /**
     * Reads a {@code .vflib.json} back, strictly: UTF-8 (a byte order mark is allowed), one JSON object with
     * {@code formatVersion} 1, a {@code MAJOR.MINOR.PATCH} {@code engineVersion}, {@code hashAlgorithm "SHA-256"},
     * {@code lineEndings "lf"} and {@code files}, each a {@linkplain #isValidPath valid path} to 64 lowercase hex
     * digits, no two the same when compared {@linkplain #pathKey ignoring case}. A member that appears twice is an
     * error; other members are ignored; entries that are not {@linkplain #isLibraryFile library files} are skipped.
     * <p>
     * A newer {@code formatVersion} is reported before anything else, because a newer launcher may have changed every
     * other member.
     */
    public static Contents parse(byte[] bytes) throws InvalidLibraryManifestException {
        Objects.requireNonNull(bytes, "bytes");
        String text;
        try {
            text = StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes))
                    .toString();
        } catch (CharacterCodingException e) {
            throw invalid("it is not UTF-8 text");
        }
        if (!text.isEmpty() && text.charAt(0) == BYTE_ORDER_MARK) {
            text = text.substring(1);
        }

        String formatVersion = null;
        String engineVersion = null;
        String hashAlgorithm = null;
        String lineEndings = null;
        Map<String, String> files = null;
        // The first problem of the other members, reported only after the version has been checked.
        String problem = null;

        try (JsonReader reader = new JsonReader(new StringReader(text))) {
            reader.setStrictness(Strictness.STRICT);
            if (reader.peek() != JsonToken.BEGIN_OBJECT) {
                throw invalid("its top level is not a JSON object");
            }
            reader.beginObject();
            Set<String> names = new HashSet<>();
            while (reader.hasNext()) {
                String name = reader.nextName();
                if (!names.add(name)) {
                    problem = first(problem, "\"" + name + "\" appears twice");
                    reader.skipValue();
                    continue;
                }
                switch (name) {
                    case MEMBER_FORMAT_VERSION -> {
                        if (reader.peek() == JsonToken.NUMBER) {
                            formatVersion = reader.nextString();
                        } else {
                            reader.skipValue();
                            formatVersion = "";
                        }
                    }
                    case MEMBER_ENGINE_VERSION -> engineVersion = stringOrNull(reader);
                    case MEMBER_HASH_ALGORITHM -> hashAlgorithm = stringOrNull(reader);
                    case MEMBER_LINE_ENDINGS -> lineEndings = stringOrNull(reader);
                    case MEMBER_FILES -> {
                        if (reader.peek() != JsonToken.BEGIN_OBJECT) {
                            reader.skipValue();
                            problem = first(problem, "\"" + MEMBER_FILES + "\" is not an object");
                        } else {
                            FilesRead read = readFiles(reader);
                            files = read.files();
                            problem = first(problem, read.problem());
                        }
                    }
                    default -> reader.skipValue();
                }
            }
            reader.endObject();
            if (reader.peek() != JsonToken.END_DOCUMENT) {
                throw invalid("there is more data after the JSON object");
            }
        } catch (IOException | IllegalStateException | NumberFormatException e) {
            // Gson reports malformed JSON as MalformedJsonException (an IOException) or IllegalStateException.
            throw invalid("it is not valid JSON: " + e.getMessage());
        }

        // The version first: a newer file may have changed any other member.
        if (formatVersion == null) {
            throw invalid("it has no \"" + MEMBER_FORMAT_VERSION + "\"");
        }
        if (!WHOLE_NUMBER.matcher(formatVersion).matches()) {
            throw invalid("\"" + MEMBER_FORMAT_VERSION + "\" is not a whole number of at least 1");
        }
        int version = Integer.parseInt(formatVersion);
        if (version > FORMAT_VERSION) {
            throw new InvalidLibraryManifestException("it has format version " + version + ", which is newer than "
                    + "this launcher reads (" + FORMAT_VERSION + ")", true);
        }
        if (problem != null) {
            throw invalid(problem);
        }
        if (engineVersion == null || VersionNumber.parse(engineVersion).isEmpty()) {
            throw invalid("\"" + MEMBER_ENGINE_VERSION + "\" is missing or not a MAJOR.MINOR.PATCH version");
        }
        if (!HASH_ALGORITHM.equals(hashAlgorithm)) {
            throw invalid("\"" + MEMBER_HASH_ALGORITHM + "\" is not \"" + HASH_ALGORITHM + "\"");
        }
        if (!LINE_ENDINGS.equals(lineEndings)) {
            throw invalid("\"" + MEMBER_LINE_ENDINGS + "\" is not \"" + LINE_ENDINGS + "\"");
        }
        if (files == null) {
            throw invalid("it has no \"" + MEMBER_FILES + "\"");
        }
        return new Contents(engineVersion, new TreeMap<>(files));
    }

    /** The {@code files} object's entries, and the first problem with one of them ({@code null} for none). */
    private record FilesRead(Map<String, String> files, String problem) {
    }

    private static FilesRead readFiles(JsonReader reader) throws IOException {
        Map<String, String> files = new HashMap<>();
        Map<String, String> byKey = new HashMap<>();
        String problem = null;
        reader.beginObject();
        while (reader.hasNext()) {
            String path = reader.nextName();
            String hash = stringOrNull(reader);
            if (!isValidPath(path)) {
                problem = first(problem, "it lists \"" + path + "\", which is not a valid library path");
                continue;
            }
            String earlier = byKey.putIfAbsent(pathKey(path), path);
            if (earlier != null) {
                problem = first(problem, earlier.equals(path)
                        ? "it lists \"" + path + "\" twice"
                        : "it lists both \"" + earlier + "\" and \"" + path + "\", which Windows sees as one file");
                continue;
            }
            if (hash == null || !HASH_PATTERN.matcher(hash).matches()) {
                problem = first(problem, "the hash of \"" + path + "\" is not 64 lowercase hex digits");
                continue;
            }
            if (isLibraryFile(path)) {
                files.put(path, hash);
            }
        }
        reader.endObject();
        return new FilesRead(files, problem);
    }

    /** The next value when it is a string; otherwise it is skipped and {@code null} returned. */
    private static String stringOrNull(JsonReader reader) throws IOException {
        if (reader.peek() == JsonToken.STRING) {
            return reader.nextString();
        }
        reader.skipValue();
        return null;
    }

    private static String first(String problem, String candidate) {
        return problem != null ? problem : candidate;
    }

    private static InvalidLibraryManifestException invalid(String message) {
        return new InvalidLibraryManifestException(message, false);
    }

    /** The lowercase hex SHA-256 of {@code bytes} with every CRLF replaced by LF (a lone CR is kept). */
    public static String hash(byte[] bytes) {
        MessageDigest digest = sha256();
        byte[] buffer = new byte[8192];
        int used = 0;
        for (int i = 0; i < bytes.length; i++) {
            byte b = bytes[i];
            if (b == '\r' && i + 1 < bytes.length && bytes[i + 1] == '\n') {
                continue;
            }
            buffer[used++] = b;
            if (used == buffer.length) {
                digest.update(buffer, 0, used);
                used = 0;
            }
        }
        digest.update(buffer, 0, used);
        return HexFormat.of().formatHex(digest.digest());
    }

    /**
     * The file's content: UTF-8, LF line endings, no byte order mark.
     *
     * @param engineVersion the engine's {@code MAJOR.MINOR.PATCH}
     * @param files         library-relative path to {@link #hash}; sorted here
     */
    public static byte[] toJson(String engineVersion, Map<String, String> files) {
        Objects.requireNonNull(engineVersion, "engineVersion");
        JsonObject root = new JsonObject();
        root.addProperty("formatVersion", FORMAT_VERSION);
        root.addProperty("engineVersion", engineVersion);
        root.addProperty("hashAlgorithm", HASH_ALGORITHM);
        root.addProperty("lineEndings", LINE_ENDINGS);
        JsonObject hashes = new JsonObject();
        SortedMap<String, String> sorted = new TreeMap<>(files);
        sorted.forEach(hashes::addProperty);
        root.add("files", hashes);
        // Gson's pretty printing separates lines with LF on every platform.
        String json = new GsonBuilder().setPrettyPrinting().disableHtmlEscaping().create().toJson(root) + "\n";
        return json.getBytes(StandardCharsets.UTF_8);
    }

    private static MessageDigest sha256() {
        try {
            return MessageDigest.getInstance(HASH_ALGORITHM);
        } catch (NoSuchAlgorithmException e) {
            // Every Java platform must provide SHA-256.
            throw new IllegalStateException(HASH_ALGORITHM + " is not available", e);
        }
    }
}
