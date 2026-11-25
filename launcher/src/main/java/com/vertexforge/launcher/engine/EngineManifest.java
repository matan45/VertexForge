package com.vertexforge.launcher.engine;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParseException;
import com.google.gson.JsonParser;
import com.google.gson.JsonPrimitive;
import com.google.gson.Strictness;
import com.google.gson.stream.JsonReader;
import com.google.gson.stream.JsonToken;
import com.vertexforge.launcher.util.RelativePathRules;

import java.io.IOException;
import java.io.StringReader;
import java.math.BigDecimal;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.regex.Pattern;

/**
 * The {@code engine.json} beside an engine's {@code Editor.exe} (written by {@code tools/engine_version.lua}, VK-1680).
 * <pre>
 * { "manifestVersion": 1, "engineVersion": "1.0.0", "assetFormatVersion": "1.0.0", "projectSchemaVersion": "1.0",
 *   "pluginApiVersion": 25, "configuration": "Development", "gitCommit": "599a8a30…" or null,
 *   "editorExe": "Editor.exe", "workingDirectory": "../../../../VFEngine/editor",
 *   "templatesDir": "../../../../assets/templates", "scriptLibDir": "../../../../assets/scripts/lib",
 *   "examplesDir": "../../../../assets/scripts/game/examples", "vfxLibraryDir": "../../../../assets/vfx/library",
 *   "launchContract": 2, "projectCacheDirs": ["scripts/compiled"] }
 * </pre>
 * Paths are relative to the manifest's folder, except {@code projectCacheDirs}, whose entries are relative to a
 * project's working directory. The members the launcher needs to start the editor are required and checked strictly;
 * the others are informational and read as {@code null} when they are missing or of the wrong type: a bad one must
 * never make the whole engine unusable ({@code EngineProbe} reports any parse failure as a broken engine). Members this
 * launcher does not know are ignored.
 *
 * @param manifestVersion      always {@link #SUPPORTED_VERSION}; a newer manifest is rejected
 * @param engineVersion        the engine's {@code MAJOR.MINOR.PATCH}
 * @param assetFormatVersion   the binary asset format version, or {@code null}
 * @param projectSchemaVersion the {@code .vfproj} schema version, or {@code null}
 * @param pluginApiVersion     the plugin API version, or {@code null}
 * @param configuration        {@code Debug}, {@code Development} or {@code Release}
 * @param gitCommit            the commit the engine was built from, or {@code null}
 * @param editorExe            the editor's file name, in the manifest's folder
 * @param workingDirectory     the folder the editor must start in, relative to the manifest's folder
 * @param templatesDir         the project templates folder, or {@code null} (VK-1689)
 * @param scriptLibDir         the mType script library folder, or {@code null} (VK-1689)
 * @param examplesDir          the example scripts a new project may include, or {@code null} (VK-1689)
 * @param vfxLibraryDir        the VFX library a new project may include, or {@code null} (VK-1689)
 * @param launchContract      the editor launch-contract revision ({@code launch::editorLaunchContract}, VK-1683), or
 *                             {@code null} for an editor built before the field existed: the launcher then passes only
 *                             the arguments every editor accepts. 1 adds {@code --console}; 2 means the arguments
 *                             may hold any Unicode character (the editor runs in the UTF-8 code page, VK-1684).
 * @param projectCacheDirs     the folders in a project that the editor rebuilds and Clean Caches may delete, relative
 *                             to the project's working directory (VK-1695); {@code null} when the member is missing or
 *                             not a valid list (Clean Caches is then unavailable with this engine), empty when the
 *                             engine rebuilds nothing
 */
public record EngineManifest(
        int manifestVersion,
        String engineVersion,
        String assetFormatVersion,
        String projectSchemaVersion,
        Integer pluginApiVersion,
        String configuration,
        String gitCommit,
        String editorExe,
        String workingDirectory,
        String templatesDir,
        String scriptLibDir,
        String examplesDir,
        String vfxLibraryDir,
        Integer launchContract,
        List<String> projectCacheDirs
) {
    public EngineManifest {
        projectCacheDirs = projectCacheDirs == null ? null : List.copyOf(projectCacheDirs);
    }

    public static final String FILE_NAME = "engine.json";
    public static final int SUPPORTED_VERSION = 1;
    public static final List<String> CONFIGURATIONS = List.of("Debug", "Development", "Release");

    static final String MANIFEST_VERSION = "manifestVersion";
    static final String ENGINE_VERSION = "engineVersion";
    static final String ASSET_FORMAT_VERSION = "assetFormatVersion";
    static final String PROJECT_SCHEMA_VERSION = "projectSchemaVersion";
    static final String PLUGIN_API_VERSION = "pluginApiVersion";
    static final String CONFIGURATION = "configuration";
    static final String GIT_COMMIT = "gitCommit";
    static final String EDITOR_EXE = "editorExe";
    static final String WORKING_DIRECTORY = "workingDirectory";
    static final String TEMPLATES_DIR = "templatesDir";
    static final String SCRIPT_LIB_DIR = "scriptLibDir";
    static final String EXAMPLES_DIR = "examplesDir";
    static final String VFX_LIBRARY_DIR = "vfxLibraryDir";
    static final String LAUNCH_CONTRACT = "launchContract";
    static final String PROJECT_CACHE_DIRS = "projectCacheDirs";
    /** More entries than any engine needs make the list unusable rather than slow (VK-1695). */
    static final int MAX_PROJECT_CACHE_DIRS = 32;
    /** The first contract revision with {@code --console} (VK-1683). */
    public static final int CONSOLE_FLAG_CONTRACT = 1;
    /**
     * The first contract revision whose editor receives its arguments in the UTF-8 code page (VK-1684): an older one
     * gets them in the system's ANSI code page, so characters that code page lacks arrive damaged.
     */
    public static final int UNICODE_ARGUMENTS_CONTRACT = 2;

    /** {@code MAJOR.MINOR.PATCH} without leading zeros, as {@code tools/engine_version.lua} writes it. */
    private static final Pattern VERSION_PATTERN = Pattern.compile("(0|[1-9]\\d*)(\\.(0|[1-9]\\d*)){2}");
    private static final char BYTE_ORDER_MARK = '﻿';

    /** Parses a manifest's text. */
    public static EngineManifest parse(String text) throws InvalidManifestException {
        if (!text.isEmpty() && text.charAt(0) == BYTE_ORDER_MARK) {
            text = text.substring(1);
        }

        JsonElement element;
        try {
            JsonReader reader = new JsonReader(new StringReader(text));
            // parseReader only relaxes a reader left at the default LEGACY_STRICT, so this stays strict.
            reader.setStrictness(Strictness.STRICT);
            element = JsonParser.parseReader(reader);
            if (!element.isJsonObject()) {
                throw new InvalidManifestException(element.isJsonNull()
                        ? "it is empty"
                        : "its top level is not a JSON object");
            }
            if (reader.peek() != JsonToken.END_DOCUMENT) {
                throw new InvalidManifestException("there is more data after the JSON document");
            }
        } catch (JsonParseException | IOException e) {
            throw new InvalidManifestException("it is not valid JSON: " + e.getMessage());
        }
        JsonObject root = element.getAsJsonObject();

        // The version first: a newer manifest may have changed any other member.
        JsonElement versionElement = root.get(MANIFEST_VERSION);
        Integer version = wholeNumber(versionElement);
        if (version == null || version < 1) {
            throw new InvalidManifestException("\"" + MANIFEST_VERSION + "\" is not a whole number of at least 1");
        }
        if (version > SUPPORTED_VERSION) {
            throw new InvalidManifestException("it has manifest version " + version
                    + ", which is newer than this launcher supports (" + SUPPORTED_VERSION + ")");
        }

        String engineVersion = requiredString(root, ENGINE_VERSION);
        if (!VERSION_PATTERN.matcher(engineVersion).matches()) {
            throw new InvalidManifestException("\"" + ENGINE_VERSION + "\" is \"" + engineVersion
                    + "\", not a MAJOR.MINOR.PATCH version");
        }
        String configuration = requiredString(root, CONFIGURATION);
        if (!CONFIGURATIONS.contains(configuration)) {
            throw new InvalidManifestException("\"" + CONFIGURATION + "\" is \"" + configuration
                    + "\", not one of " + String.join(", ", CONFIGURATIONS));
        }
        String editorExe = requiredString(root, EDITOR_EXE);
        if (!isBareFileName(editorExe)) {
            throw new InvalidManifestException("\"" + EDITOR_EXE + "\" is \"" + editorExe
                    + "\", not a file name in the manifest's folder");
        }
        String workingDirectory = requiredString(root, WORKING_DIRECTORY);
        if (workingDirectory.isBlank() || !isValidPath(workingDirectory)) {
            throw new InvalidManifestException("\"" + WORKING_DIRECTORY + "\" is \"" + workingDirectory
                    + "\", not a valid path");
        }

        return new EngineManifest(
                version,
                engineVersion,
                optionalString(root, ASSET_FORMAT_VERSION),
                optionalString(root, PROJECT_SCHEMA_VERSION),
                wholeNumber(root.get(PLUGIN_API_VERSION)),
                configuration,
                optionalString(root, GIT_COMMIT),
                editorExe,
                workingDirectory,
                optionalString(root, TEMPLATES_DIR),
                optionalString(root, SCRIPT_LIB_DIR),
                optionalString(root, EXAMPLES_DIR),
                optionalString(root, VFX_LIBRARY_DIR),
                launchContract(root.get(LAUNCH_CONTRACT)),
                projectCacheDirs(root.get(PROJECT_CACHE_DIRS)));
    }

    /**
     * The {@code projectCacheDirs} member as a list, or {@code null} when it is missing or not a list Clean Caches can
     * trust (VK-1695). Lenient by design: it never throws, so a bad list costs only that action, never the engine. The
     * whole list is refused when any entry is not a string, is not a {@link RelativePathRules relative path}, lies in
     * the editor's own folder, repeats another entry (ignoring case) or lies in one; or when there are more than
     * {@link #MAX_PROJECT_CACHE_DIRS} of them.
     */
    static List<String> projectCacheDirs(JsonElement element) {
        if (element == null || !element.isJsonArray()) {
            return null;
        }
        JsonArray array = element.getAsJsonArray();
        if (array.size() > MAX_PROJECT_CACHE_DIRS) {
            return null;
        }
        List<String> entries = new ArrayList<>();
        for (JsonElement item : array) {
            if (item == null || !item.isJsonPrimitive() || !item.getAsJsonPrimitive().isString()) {
                return null;
            }
            String entry = item.getAsString();
            if (!RelativePathRules.isValid(entry) || RelativePathRules.isEditorState(entry)) {
                return null;
            }
            for (String other : entries) {
                if (sameOrInside(entry, other) || sameOrInside(other, entry)) {
                    return null;
                }
            }
            entries.add(entry);
        }
        return List.copyOf(entries);
    }

    /** Whether the '/'-relative {@code path} is {@code base} or lies in it, ignoring case. */
    private static boolean sameOrInside(String path, String base) {
        return path.equalsIgnoreCase(base)
                || (path.length() > base.length() && path.charAt(base.length()) == '/'
                && path.regionMatches(true, 0, base, 0, base.length()));
    }

    /** Whether this engine's editor accepts {@code --console} (VK-1683). */
    public boolean acceptsConsoleFlag() {
        return launchContract != null && launchContract >= CONSOLE_FLAG_CONTRACT;
    }

    /** Whether this engine's editor receives any Unicode argument undamaged (VK-1684). */
    public boolean acceptsUnicodeArguments() {
        return launchContract != null && launchContract >= UNICODE_ARGUMENTS_CONTRACT;
    }

    /** A whole number of at least 1; {@code null} otherwise, which the launcher treats as an unknown contract. */
    private static Integer launchContract(JsonElement element) {
        Integer value = wholeNumber(element);
        return value != null && value >= 1 ? value : null;
    }

    /**
     * Reads {@code dir/engine.json}. Blocks on the file system, so it only runs on a background thread.
     *
     * @return empty when there is no such file (an install without a manifest)
     * @throws IOException              when the file exists but cannot be read
     * @throws InvalidManifestException when it is not a manifest this launcher can use
     */
    public static Optional<EngineManifest> readIfPresent(Path dir) throws IOException, InvalidManifestException {
        try {
            return Optional.of(read(dir.resolve(FILE_NAME)));
        } catch (NoSuchFileException e) {
            return Optional.empty();
        }
    }

    /**
     * Reads a manifest file, which must be strict UTF-8 (a leading byte order mark is allowed). Blocks on the file
     * system, so it only runs on a background thread.
     *
     * @throws NoSuchFileException      when there is no such file
     * @throws IOException              when it cannot be read
     * @throws InvalidManifestException when it is not a manifest this launcher can use
     */
    public static EngineManifest read(Path file) throws IOException, InvalidManifestException {
        byte[] bytes = Files.readAllBytes(file);
        String text;
        try {
            text = StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes))
                    .toString();
        } catch (CharacterCodingException e) {
            throw new InvalidManifestException("it is not valid UTF-8 text");
        }
        return parse(text);
    }

    /** The folder the editor starts in, for a manifest that lives in {@code dir}. */
    public Path workingDirectoryIn(Path dir) {
        return dir.resolve(workingDirectory).normalize();
    }

    /** The project templates folder for a manifest in {@code dir} (VK-1689); empty when unknown. */
    public Optional<Path> templatesDirIn(Path dir) {
        return resolveIn(dir, templatesDir);
    }

    /** The mType script library folder for a manifest in {@code dir} (VK-1689); empty when unknown. */
    public Optional<Path> scriptLibDirIn(Path dir) {
        return resolveIn(dir, scriptLibDir);
    }

    /** The example scripts folder for a manifest in {@code dir} (VK-1689); empty when unknown. */
    public Optional<Path> examplesDirIn(Path dir) {
        return resolveIn(dir, examplesDir);
    }

    /** The VFX library folder for a manifest in {@code dir} (VK-1689); empty when unknown. */
    public Optional<Path> vfxLibraryDirIn(Path dir) {
        return resolveIn(dir, vfxLibraryDir);
    }

    /** {@code 1.0.0 Development}. */
    public String describe() {
        return engineVersion + " " + configuration;
    }

    private static Optional<Path> resolveIn(Path dir, String relative) {
        if (relative == null || relative.isBlank() || !isValidPath(relative)) {
            return Optional.empty();
        }
        return Optional.of(dir.resolve(relative).normalize());
    }

    private static boolean isBareFileName(String name) {
        if (name.isBlank() || name.equals(".") || name.equals("..")
                || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 || !isValidPath(name)) {
            return false;
        }
        Path path = Path.of(name);
        return path.getNameCount() == 1 && path.getRoot() == null && name.equals(path.toString());
    }

    private static boolean isValidPath(String text) {
        try {
            Path.of(text);
            return true;
        } catch (InvalidPathException e) {
            return false;
        }
    }

    private static String requiredString(JsonObject root, String key) throws InvalidManifestException {
        String value = optionalString(root, key);
        if (value == null) {
            throw new InvalidManifestException("\"" + key + "\" is missing or not a string");
        }
        return value;
    }

    /** The member as a string; {@code null} when it is absent, JSON {@code null} or not a string. */
    private static String optionalString(JsonObject root, String key) {
        JsonElement element = root.get(key);
        if (element == null || !element.isJsonPrimitive() || !element.getAsJsonPrimitive().isString()) {
            return null;
        }
        return element.getAsString();
    }

    /** The member as a whole number that fits an {@code int}; {@code null} otherwise. */
    private static Integer wholeNumber(JsonElement element) {
        if (element == null || !element.isJsonPrimitive()) {
            return null;
        }
        JsonPrimitive primitive = element.getAsJsonPrimitive();
        if (!primitive.isNumber()) {
            return null;
        }
        try {
            BigDecimal value = primitive.getAsBigDecimal();
            return value.intValueExact();
        } catch (ArithmeticException | NumberFormatException e) {
            return null;
        }
    }
}
