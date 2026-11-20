package com.vertexforge.launcher.project;

import com.google.gson.FormattingStyle;
import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParseException;
import com.google.gson.JsonParser;
import com.google.gson.JsonPrimitive;
import com.google.gson.Strictness;
import com.google.gson.stream.JsonReader;
import com.google.gson.stream.JsonToken;

import java.io.IOException;
import java.io.StringReader;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/**
 * Rewrites a {@code .vfproj} for a copy of its project (VK-1690: "Open a copy"). Everything the file holds is kept, in
 * its order: members this launcher does not know, nested values, numbers exactly as written, and the line ends. Only
 * these change:
 * <ul>
 *     <li>{@code projectName} becomes the copy's name;</li>
 *     <li>a path member that leads into the original folder but, as written, would not lead to the same place in the
 *     copy: an absolute path, or a relative one whose {@code ..} leaves the folder and comes back into it by its name.
 *     Left as it is, the copy's editor would write into the ORIGINAL (scenes, {@code .vfmeta} files, the input
 *     mapping, which the editor writes through). {@code workingDirectory} then becomes relative to the copy's folder
 *     ({@code "."} for the folder itself); {@code startupScene}, {@code exeIconPath} and {@code inputMapping} become the
 *     same place in the copy: relative to its working directory when they lie in it and were relative, else
 *     absolute.</li>
 * </ul>
 * A path is resolved as the editor resolves it ({@code ProjectServiceImpl::loadProject}): the working directory against
 * the project file's folder, the other members against the working directory, then normalized. Paths that lead outside
 * the original folder, and relative paths that already lead to the same place in the copy, are kept as they are. Paths
 * are compared name by name, ignoring case, as Windows does.
 * <p>
 * The file is read as strictly as {@link ProjectFileReader} reads it, with its messages.
 */
public final class ProjectFileEdit {

    static final String PROJECT_NAME = ProjectFileReader.PROJECT_NAME;
    static final String WORKING_DIRECTORY = "workingDirectory";
    /** The members holding a path that the editor resolves against the working directory when it is relative. */
    static final List<String> PATH_MEMBERS = List.of("startupScene", "exeIconPath", "inputMapping");

    private static final char BYTE_ORDER_MARK = '﻿';

    private ProjectFileEdit() {
        // Utility class - prevent instantiation
    }

    /**
     * The bytes of the copy's {@code .vfproj}: UTF-8 without a byte order mark, pretty-printed with two spaces, with
     * CRLF line ends when the original has any, and a final line end when the original ends with one.
     *
     * @param original       the original {@code .vfproj}'s bytes
     * @param newName        the copy's {@code projectName}
     * @param originalFolder the folder that holds the original {@code .vfproj}, absolute
     * @param copyFolder     the folder the copy will be in, absolute
     * @throws InvalidProjectFileException when the original is not a project file the launcher can read
     */
    public static byte[] forCopy(byte[] original, String newName, Path originalFolder, Path copyFolder)
            throws InvalidProjectFileException {
        Objects.requireNonNull(original, "original");
        Objects.requireNonNull(newName, "newName");
        Path from = absolute(originalFolder, "originalFolder");
        Path to = absolute(copyFolder, "copyFolder");

        String text = decode(original);
        JsonObject root = parseObject(text);

        // JsonObject keeps its members in order, and replacing a member's value keeps its place.
        root.addProperty(PROJECT_NAME, newName);

        // Where the editor finds the working directory in the original, and where it must find it in the copy.
        String workingDirectory = pathText(root, WORKING_DIRECTORY);
        // Without a usable working directory, the members are taken as relative to each project's own folder.
        Path originalBase = from;
        Path copyBase = to;
        if (workingDirectory != null) {
            Path resolved = resolve(from, workingDirectory);
            List<String> below = resolved == null ? null : ProjectContent.namesBelow(from, resolved);
            if (below != null) {
                originalBase = resolved;
                copyBase = under(to, below);
                if (!leadsTo(to, workingDirectory, copyBase)) {
                    root.addProperty(WORKING_DIRECTORY, below.isEmpty() ? "." : String.join("/", below));
                }
            } else if (resolved != null) {
                // Outside the original folder: both use it as it is (the copier refuses such a project anyway).
                originalBase = resolved;
                copyBase = resolved;
            }
        }

        for (String member : PATH_MEMBERS) {
            String value = pathText(root, member);
            Path resolved = value == null ? null : resolve(originalBase, value);
            List<String> below = resolved == null ? null : ProjectContent.namesBelow(from, resolved);
            if (below == null) {
                continue;
            }
            Path mirrored = under(to, below);
            if (leadsTo(copyBase, value, mirrored)) {
                continue;
            }
            List<String> inBase = Path.of(value).isAbsolute() ? null : ProjectContent.namesBelow(copyBase, mirrored);
            root.addProperty(member, inBase != null && !inBase.isEmpty()
                    ? String.join(separatorOf(value), inBase)
                    : inSeparatorsOf(value, mirrored.toString()));
        }

        String newline = text.contains("\r\n") ? "\r\n" : "\n";
        Gson gson = new GsonBuilder()
                .setFormattingStyle(FormattingStyle.PRETTY.withNewline(newline))
                .serializeNulls()
                .disableHtmlEscaping()
                .create();
        String out = gson.toJson(root);
        if (text.endsWith("\n")) {
            out += newline;
        }
        return out.getBytes(StandardCharsets.UTF_8);
    }

    /**
     * A path member of a {@code .vfproj}, resolved as the editor resolves it.
     *
     * @param member   the member's name
     * @param text     its text
     * @param resolved where it leads; {@code null} when the launcher cannot tell (not a path it can read, or a
     *                 drive-relative one such as {@code D:assets})
     */
    public record MemberPath(String member, String text, Path resolved) {
        public MemberPath {
            Objects.requireNonNull(member, "member");
            Objects.requireNonNull(text, "text");
        }
    }

    /**
     * The path members of the {@code .vfproj} {@code bytes} as the editor resolves them for a project file in
     * {@code folder}: {@code workingDirectory} against the folder, the others against the working directory. Members
     * that are absent, blank or not strings are left out. "Open a copy" checks these on the file system against the
     * original folder, which also catches the spellings this class compares by text only (a trailing dot, an admin
     * share, a short name, a link).
     *
     * @throws InvalidProjectFileException when the bytes are not a project file the launcher can read
     */
    public static List<MemberPath> resolvedPaths(byte[] bytes, Path folder) throws InvalidProjectFileException {
        Objects.requireNonNull(bytes, "bytes");
        Path base = absolute(folder, "folder");
        JsonObject root = parseObject(decode(bytes));
        List<MemberPath> paths = new ArrayList<>();
        Path membersBase = base;
        String workingDirectory = stringMember(root, WORKING_DIRECTORY);
        if (workingDirectory != null) {
            Path resolved = resolveText(base, workingDirectory);
            paths.add(new MemberPath(WORKING_DIRECTORY, workingDirectory, resolved));
            if (resolved != null) {
                membersBase = resolved;
            }
        }
        for (String member : PATH_MEMBERS) {
            String text = stringMember(root, member);
            if (text != null) {
                paths.add(new MemberPath(member, text, resolveText(membersBase, text)));
            }
        }
        return List.copyOf(paths);
    }

    /** The member's text when it is a non-blank string, whether or not it is a valid path. */
    private static String stringMember(JsonObject root, String member) {
        JsonElement element = root.get(member);
        if (element == null || !element.isJsonPrimitive() || !element.getAsJsonPrimitive().isString()) {
            return null;
        }
        String text = element.getAsString();
        return text.isBlank() ? null : text;
    }

    /** {@link #resolve}, or {@code null} when {@code text} is not a path the launcher can read. */
    private static Path resolveText(Path base, String text) {
        try {
            return resolve(base, text);
        } catch (InvalidPathException e) {
            return null;
        }
    }

    private static Path absolute(Path folder, String what) {
        Objects.requireNonNull(folder, what);
        if (!folder.isAbsolute()) {
            throw new IllegalArgumentException(what + " must be absolute: " + folder);
        }
        return folder.normalize();
    }

    /** As {@link ProjectFileReader#read}: at most its size limit, strict UTF-8. */
    private static String decode(byte[] bytes) throws InvalidProjectFileException {
        if (bytes.length > ProjectFileReader.MAX_BYTES) {
            throw new InvalidProjectFileException("The file is larger than "
                    + (ProjectFileReader.MAX_BYTES / (1024 * 1024)) + " MiB, which no project file is");
        }
        try {
            return StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes))
                    .toString();
        } catch (CharacterCodingException e) {
            throw new InvalidProjectFileException("The file is not valid UTF-8 text");
        }
    }

    /** As {@link ProjectFileReader#parse}: strict JSON, an object, nothing after it. */
    private static JsonObject parseObject(String text) throws InvalidProjectFileException {
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
            throw ProjectFileReader.invalidJson(e.getCause() != null ? e.getCause() : e, null);
        }
        if (element.isJsonNull()) {
            throw new InvalidProjectFileException("The file is empty");
        }
        if (!element.isJsonObject()) {
            throw new InvalidProjectFileException("The file's top level is not a JSON object");
        }
        try {
            if (reader.peek() != JsonToken.END_DOCUMENT) {
                throw new InvalidProjectFileException("Not valid JSON: " + ProjectFileReader.MORE_DATA);
            }
        } catch (IOException e) {
            throw ProjectFileReader.invalidJson(e, ProjectFileReader.MORE_DATA);
        }
        return element.getAsJsonObject();
    }

    /** The member's text when it is a non-blank string that is a valid path; {@code null} otherwise. */
    private static String pathText(JsonObject root, String member) {
        JsonElement element = root.get(member);
        if (element == null || !element.isJsonPrimitive()) {
            return null;
        }
        JsonPrimitive primitive = element.getAsJsonPrimitive();
        if (!primitive.isString() || primitive.getAsString().isBlank()) {
            return null;
        }
        String text = primitive.getAsString();
        try {
            Path.of(text);
        } catch (InvalidPathException e) {
            return null;
        }
        return text;
    }

    /**
     * {@code text} resolved against {@code base} and normalized, as the editor resolves it; {@code null} when the result
     * is not absolute (a drive-relative path such as {@code D:assets}, which leads nowhere in the folder).
     */
    private static Path resolve(Path base, String text) {
        Path resolved = base.resolve(Path.of(text)).normalize();
        return resolved.isAbsolute() ? resolved : null;
    }

    /** {@code base} with {@code names} below it. */
    private static Path under(Path base, List<String> names) {
        Path path = base;
        for (String name : names) {
            path = path.resolve(name);
        }
        return path;
    }

    /** Whether {@code text}, resolved against {@code base} as the editor resolves it, is {@code target}. */
    private static boolean leadsTo(Path base, String text, Path target) {
        Path resolved = resolve(base, text);
        List<String> below = resolved == null ? null : ProjectContent.namesBelow(target, resolved);
        return below != null && below.isEmpty();
    }

    /** "\" when {@code text} separates its names with backslashes only; "/" otherwise. */
    private static String separatorOf(String text) {
        return text.indexOf('/') < 0 && text.indexOf('\\') >= 0 ? "\\" : "/";
    }

    /** {@code produced} written with '/' when {@code original} used only '/' between its names. */
    private static String inSeparatorsOf(String original, String produced) {
        return original.indexOf('\\') < 0 && original.indexOf('/') >= 0 ? produced.replace('\\', '/') : produced;
    }
}
