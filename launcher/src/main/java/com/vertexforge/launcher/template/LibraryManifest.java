package com.vertexforge.launcher.template;

import com.google.gson.GsonBuilder;
import com.google.gson.JsonObject;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.HexFormat;
import java.util.Map;
import java.util.Objects;
import java.util.SortedMap;
import java.util.TreeMap;

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
 */
public final class LibraryManifest {

    public static final String FILE_NAME = ".vflib.json";
    public static final int FORMAT_VERSION = 1;
    public static final String HASH_ALGORITHM = "SHA-256";
    public static final String LINE_ENDINGS = "lf";

    private LibraryManifest() {
        // Utility class - prevent instantiation
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
