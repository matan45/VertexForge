package com.vertexforge.launcher.project;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParseException;
import com.google.gson.JsonParser;
import com.google.gson.Strictness;
import com.google.gson.stream.JsonReader;
import com.google.gson.stream.JsonToken;

import java.io.IOException;
import java.io.InputStream;
import java.io.StringReader;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Locale;
import java.util.Objects;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Reads the members of a {@code .vfproj} that the launcher uses (VK-1685).
 * <p>
 * The JSON must be strict, as the editor's reader (nlohmann::json) wants it: no comments, single quotes, unquoted
 * names, trailing commas, {@code NaN} or data after the document. Everything else is tolerated: a byte order mark,
 * CRLF line ends, any key order, members this launcher does not know, and versions of the wrong type (read as
 * {@code null}). Only {@code projectName} is required, because the list shows it and the editor refuses a project
 * without one.
 */
public final class ProjectFileReader {

    /** Larger files are refused unread; the editor writes a few hundred bytes. */
    public static final int MAX_BYTES = 1024 * 1024;

    static final String PROJECT_NAME = "projectName";
    static final String SCHEMA_VERSION = "schemaVersion";
    static final String ENGINE_VERSION = "engineVersion";

    private static final char BYTE_ORDER_MARK = '﻿';
    /** Gson's location, which every syntax error carries: {@code " at line 3 column 8 path $.x"}. */
    private static final Pattern LOCATION = Pattern.compile(" at line (\\d+) column (\\d+)");
    /** Gson 2.13's whole message for comments, single quotes, unquoted names, NaN, bad literals and trailing data. */
    private static final String GSON_STRICTNESS_HINT = "Use JsonReader.setStrictness";
    private static final String GSON_END_OF_INPUT = "End of input";

    static final String NOT_STRICT_JSON =
            "comments, single quotes, unquoted names and trailing commas are not allowed";
    static final String ENDS_TOO_EARLY = "the file ends before the JSON is complete";
    static final String MORE_DATA = "there is more data after the JSON document";
    static final String MALFORMED = "the JSON is malformed";

    private ProjectFileReader() {
        // Utility class - prevent instantiation
    }

    /**
     * Parses a project file's text.
     *
     * @throws InvalidProjectFileException when it is not strict JSON, not an object, or has no usable
     *                                     {@code projectName}
     */
    public static ProjectFile parse(String text) throws InvalidProjectFileException {
        Objects.requireNonNull(text, "text");
        // Gson skips a byte order mark by itself (columns stay as without it); removing it here does not depend on that.
        if (!text.isEmpty() && text.charAt(0) == BYTE_ORDER_MARK) {
            text = text.substring(1);
        }

        JsonReader reader = new JsonReader(new StringReader(text));
        // parseReader only relaxes a reader left at the default LEGACY_STRICT, so this stays strict.
        reader.setStrictness(Strictness.STRICT);
        JsonElement element;
        try {
            element = JsonParser.parseReader(reader);
        } catch (JsonParseException e) {
            // JsonSyntaxException's own message starts with the cause's class name; the cause's is the readable one.
            throw invalidJson(e.getCause() != null ? e.getCause() : e, null);
        }
        if (element.isJsonNull()) {
            throw new InvalidProjectFileException("The file is empty");
        }
        if (!element.isJsonObject()) {
            throw new InvalidProjectFileException("The file's top level is not a JSON object");
        }
        try {
            if (reader.peek() != JsonToken.END_DOCUMENT) {
                throw new InvalidProjectFileException("Not valid JSON: " + MORE_DATA);
            }
        } catch (IOException e) {
            // Strict mode refuses a second value at once, with Gson's generic message and the place it starts.
            throw invalidJson(e, MORE_DATA);
        }

        JsonObject root = element.getAsJsonObject();
        return new ProjectFile(projectName(root), optionalString(root, SCHEMA_VERSION),
                optionalString(root, ENGINE_VERSION));
    }

    /**
     * Reads a project file, which must be strict UTF-8 (a leading byte order mark is allowed) and at most
     * {@link #MAX_BYTES} long. Blocks on the file system, so it only runs on a background thread.
     *
     * @throws java.nio.file.NoSuchFileException when there is no such file
     * @throws IOException                       when it cannot be read
     * @throws InvalidProjectFileException       when it is not a project file this launcher can use
     */
    public static ProjectFile read(Path file) throws IOException, InvalidProjectFileException {
        Objects.requireNonNull(file, "file");
        byte[] bytes;
        try (InputStream in = Files.newInputStream(file)) {
            // One byte more than allowed tells a file at the limit from a longer one, without trusting its size.
            bytes = in.readNBytes(MAX_BYTES + 1);
        }
        if (bytes.length > MAX_BYTES) {
            throw new InvalidProjectFileException("The file is larger than " + (MAX_BYTES / (1024 * 1024))
                    + " MiB, which no project file is");
        }
        String text;
        try {
            text = StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes))
                    .toString();
        } catch (CharacterCodingException e) {
            throw new InvalidProjectFileException("The file is not valid UTF-8 text");
        }
        return parse(text);
    }

    private static String projectName(JsonObject root) throws InvalidProjectFileException {
        JsonElement element = root.get(PROJECT_NAME);
        if (element == null || element.isJsonNull()) {
            throw new InvalidProjectFileException("The file has no \"" + PROJECT_NAME + "\"");
        }
        if (!element.isJsonPrimitive() || !element.getAsJsonPrimitive().isString()) {
            throw new InvalidProjectFileException("Its \"" + PROJECT_NAME + "\" is not a string");
        }
        String name = element.getAsString();
        if (name.isBlank()) {
            throw new InvalidProjectFileException("Its \"" + PROJECT_NAME + "\" is empty");
        }
        return name;
    }

    /** The member as a string; {@code null} when it is absent, JSON {@code null} or not a string. */
    private static String optionalString(JsonObject root, String key) {
        JsonElement element = root.get(key);
        if (element == null || !element.isJsonPrimitive() || !element.getAsJsonPrimitive().isString()) {
            return null;
        }
        return element.getAsString();
    }

    /**
     * "Not valid JSON at line L, column C: reason" from a Gson error. The reason is Gson's text before its location
     * (which also drops its "See https://…" line), with Gson's generic strictness advice replaced by what it means
     * here; {@code reasonOverride} replaces it outright. The reason is never empty.
     */
    static InvalidProjectFileException invalidJson(Throwable error, String reasonOverride) {
        String message = error.getMessage() == null ? "" : error.getMessage();
        Matcher location = LOCATION.matcher(message);
        boolean located = location.find();
        String gsonReason = (located ? message.substring(0, location.start()) : firstLine(message)).strip();

        String reason = reasonOverride != null ? reasonOverride : readable(gsonReason);
        if (!located) {
            return new InvalidProjectFileException("Not valid JSON: " + reason);
        }
        int line = Integer.parseInt(location.group(1));
        int column = Integer.parseInt(location.group(2));
        return new InvalidProjectFileException("Not valid JSON at line " + line + ", column " + column + ": " + reason,
                line, column);
    }

    private static String readable(String gsonReason) {
        if (gsonReason.isEmpty()) {
            return MALFORMED;
        }
        if (gsonReason.startsWith(GSON_STRICTNESS_HINT)) {
            return NOT_STRICT_JSON;
        }
        if (gsonReason.startsWith(GSON_END_OF_INPUT)) {
            return ENDS_TOO_EARLY;
        }
        // Gson's reasons start with a capital ("Expected ':'"); after the colon they read as the rest of a sentence.
        return gsonReason.substring(0, 1).toLowerCase(Locale.ROOT) + gsonReason.substring(1);
    }

    private static String firstLine(String text) {
        int end = text.indexOf('\n');
        return end < 0 ? text : text.substring(0, end);
    }
}
