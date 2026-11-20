package com.vertexforge.launcher.template;

import com.google.gson.JsonArray;
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
import java.math.BigDecimal;
import java.math.BigInteger;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Objects;
import java.util.Set;
import java.util.regex.Pattern;

/**
 * A project template's {@code template.json} (VK-1689): {@code <templatesDir>/<id>/template.json}, beside the
 * {@code project/} folder whose files a new project starts with.
 * <pre>
 * { "formatVersion": 1, "id": "empty", "name": "Empty", "description": "…", "minimumEngineVersion": "1.0.0",
 *   "workingDirectory": "assets", "startupScene": "scenes/Main.vfScene", "scriptLibrary": "assets/scripts/lib",
 *   "options": [ { "id": "examples", "name": "Include examples", "description": "…", "default": false,
 *                  "target": "assets/scripts/game/examples" } ] }
 * </pre>
 * Every path is relative, with forward slashes: {@code workingDirectory}, {@code scriptLibrary} and each option's
 * {@code target} to the project folder, {@code startupScene} to the working directory (as in the {@code .vfproj}).
 * The parse is strict: a member that is required, or present but wrong, makes the whole template unusable. Members
 * this launcher does not know are ignored.
 *
 * @param id                   the template's folder name
 * @param name                 what the dialog shows
 * @param description          one or two sentences for the dialog; may be empty
 * @param minimumEngineVersion the oldest engine ({@code MAJOR.MINOR.PATCH}) the template is for
 * @param workingDirectory     the {@code .vfproj} {@code workingDirectory}: {@code "."} or a folder in the project
 * @param startupScene         the {@code .vfproj} {@code startupScene}, a {@code .vfScene} the editor builds
 * @param scriptLibrary        where the engine's script library is copied
 * @param options              what the user may add, in the order the dialog shows them
 */
public record TemplateManifest(
        String id,
        String name,
        String description,
        String minimumEngineVersion,
        String workingDirectory,
        String startupScene,
        String scriptLibrary,
        List<Option> options
) {
    public static final String FILE_NAME = "template.json";
    /** The folder beside {@code template.json} whose files are copied into the project folder. */
    public static final String PAYLOAD_DIR = "project";
    public static final int SUPPORTED_VERSION = 1;
    /** The working directory that is the project folder itself. */
    public static final String PROJECT_FOLDER = ".";
    /** The editor's own folder in a project (VK-1682); nothing a template writes may go there. */
    static final String EDITOR_STATE_DIR = ".vertexforge";
    static final String SCENE_EXTENSION = ".vfScene";

    /** {@code MAJOR.MINOR.PATCH} without leading zeros, as the engine's {@code VERSION} file holds it. */
    static final Pattern VERSION_PATTERN = Pattern.compile("(0|[1-9]\\d*)(\\.(0|[1-9]\\d*)){2}");
    private static final Pattern ID_PATTERN = Pattern.compile("[A-Za-z0-9][A-Za-z0-9._-]{0,63}");
    private static final String FORBIDDEN_CHARACTERS = "<>:\"|?*\\";
    private static final char BYTE_ORDER_MARK = '\uFEFF';

    /**
     * Something the user may add to a new project: the engine folder {@link TemplateOptionKind} names for its id,
     * copied to {@code target}.
     *
     * @param id          {@code examples} or {@code vfxLibrary}; any other id is ignored by this launcher
     * @param name        the checkbox's text
     * @param description its tooltip; may be empty
     * @param defaultOn   whether the checkbox starts ticked
     * @param target      the folder in the project the files go to
     */
    public record Option(String id, String name, String description, boolean defaultOn, String target) {
        public Option {
            Objects.requireNonNull(id, "id");
            Objects.requireNonNull(name, "name");
            description = description == null ? "" : description;
            Objects.requireNonNull(target, "target");
        }
    }

    public TemplateManifest {
        Objects.requireNonNull(id, "id");
        Objects.requireNonNull(name, "name");
        description = description == null ? "" : description;
        Objects.requireNonNull(minimumEngineVersion, "minimumEngineVersion");
        Objects.requireNonNull(workingDirectory, "workingDirectory");
        Objects.requireNonNull(startupScene, "startupScene");
        Objects.requireNonNull(scriptLibrary, "scriptLibrary");
        options = List.copyOf(options);
    }

    /** Reads a {@code template.json}, which must be strict UTF-8 (a leading byte order mark is allowed). Blocks. */
    public static TemplateManifest read(Path file) throws IOException, TemplateException {
        byte[] bytes = Files.readAllBytes(file);
        String text;
        try {
            text = StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes))
                    .toString();
        } catch (CharacterCodingException e) {
            throw new TemplateException(FILE_NAME + " is not valid UTF-8 text");
        }
        return parse(text);
    }

    /** Parses a {@code template.json}'s text. */
    public static TemplateManifest parse(String text) throws TemplateException {
        if (!text.isEmpty() && text.charAt(0) == BYTE_ORDER_MARK) {
            text = text.substring(1);
        }
        JsonObject root = parseObject(text);

        // The version first: a newer format may have changed any other member.
        Integer version = wholeNumber(root.get("formatVersion"));
        if (version == null || version < 1) {
            throw invalid("\"formatVersion\" is not a whole number of at least 1");
        }
        if (version > SUPPORTED_VERSION) {
            throw invalid("it has format version " + version + ", which is newer than this launcher supports ("
                    + SUPPORTED_VERSION + ")");
        }

        String id = requiredString(root, "id");
        if (!ID_PATTERN.matcher(id).matches()) {
            throw invalid("\"id\" is \"" + id + "\": use letters, digits, '.', '-' and '_' (at most 64)");
        }
        String name = requiredString(root, "name");
        if (name.isBlank()) {
            throw invalid("\"name\" is empty");
        }
        String description = optionalString(root, "description");
        String minimumEngineVersion = requiredString(root, "minimumEngineVersion");
        if (!VERSION_PATTERN.matcher(minimumEngineVersion).matches()) {
            throw invalid("\"minimumEngineVersion\" is \"" + minimumEngineVersion
                    + "\", not a MAJOR.MINOR.PATCH version");
        }

        String workingDirectory = requiredString(root, "workingDirectory");
        if (!workingDirectory.equals(PROJECT_FOLDER)) {
            workingDirectory = projectPath("workingDirectory", workingDirectory);
        }
        String startupScene = relativePath("startupScene", requiredString(root, "startupScene"));
        if (!startupScene.toLowerCase(Locale.ROOT).endsWith(SCENE_EXTENSION.toLowerCase(Locale.ROOT))) {
            throw invalid("\"startupScene\" is \"" + startupScene + "\", not a " + SCENE_EXTENSION + " file");
        }
        if (workingDirectory.equals(PROJECT_FOLDER) && isEditorState(startupScene)) {
            throw invalid("\"startupScene\" lies in the editor's own " + EDITOR_STATE_DIR + " folder");
        }
        String scriptLibrary = projectPath("scriptLibrary", requiredString(root, "scriptLibrary"));

        List<Option> options = new ArrayList<>();
        JsonElement optionsElement = root.get("options");
        if (optionsElement != null && !optionsElement.isJsonNull()) {
            if (!optionsElement.isJsonArray()) {
                throw invalid("\"options\" is not an array");
            }
            Set<String> ids = new HashSet<>();
            JsonArray array = optionsElement.getAsJsonArray();
            for (int i = 0; i < array.size(); i++) {
                Option option = option(array.get(i), i);
                if (!ids.add(option.id())) {
                    throw invalid("the option \"" + option.id() + "\" is listed twice");
                }
                options.add(option);
            }
        }
        return new TemplateManifest(id, name, description, minimumEngineVersion, workingDirectory, startupScene,
                scriptLibrary, options);
    }

    /** {@code <workingDirectory>/<startupScene>}, relative to the project folder. */
    public String startupScenePath() {
        return workingDirectory.equals(PROJECT_FOLDER) ? startupScene : workingDirectory + "/" + startupScene;
    }

    /** Whether an engine of version {@code engineVersion} ({@code MAJOR.MINOR.PATCH}) is new enough for it. */
    public boolean supportsEngine(String engineVersion) {
        return compareVersions(engineVersion, minimumEngineVersion) >= 0;
    }

    /**
     * Compares two {@code MAJOR.MINOR.PATCH} versions numerically.
     *
     * @throws IllegalArgumentException when either is not such a version
     */
    static int compareVersions(String a, String b) {
        if (!VERSION_PATTERN.matcher(a).matches() || !VERSION_PATTERN.matcher(b).matches()) {
            throw new IllegalArgumentException("not MAJOR.MINOR.PATCH: " + a + ", " + b);
        }
        String[] left = a.split("\\.");
        String[] right = b.split("\\.");
        for (int i = 0; i < 3; i++) {
            int order = new BigInteger(left[i]).compareTo(new BigInteger(right[i]));
            if (order != 0) {
                return order;
            }
        }
        return 0;
    }

    private static Option option(JsonElement element, int index) throws TemplateException {
        if (element == null || !element.isJsonObject()) {
            throw invalid("option " + (index + 1) + " is not a JSON object");
        }
        JsonObject object = element.getAsJsonObject();
        String id = requiredString(object, "id");
        if (!ID_PATTERN.matcher(id).matches()) {
            throw invalid("option " + (index + 1) + " has the id \"" + id + "\": use letters, digits, '.', '-' and"
                    + " '_' (at most 64)");
        }
        String name = requiredString(object, "name");
        if (name.isBlank()) {
            throw invalid("the option \"" + id + "\" has an empty \"name\"");
        }
        String description = optionalString(object, "description");
        JsonElement defaultElement = object.get("default");
        boolean defaultOn = false;
        if (defaultElement != null && !defaultElement.isJsonNull()) {
            if (!defaultElement.isJsonPrimitive() || !defaultElement.getAsJsonPrimitive().isBoolean()) {
                throw invalid("the option \"" + id + "\" has a \"default\" that is not true or false");
            }
            defaultOn = defaultElement.getAsBoolean();
        }
        String target = projectPath("the option \"" + id + "\"'s \"target\"", requiredString(object, "target"));
        return new Option(id, name, description, defaultOn, target);
    }

    /** A folder or file inside the project folder: {@link #relativePath}, and never in the editor's own folder. */
    private static String projectPath(String what, String value) throws TemplateException {
        String path = relativePath(what, value);
        if (isEditorState(path)) {
            throw invalid(quoted(what) + " lies in the editor's own " + EDITOR_STATE_DIR + " folder");
        }
        return path;
    }

    /**
     * A relative path in the form the template must use: forward slashes, no empty, {@code .} or {@code ..} part, and
     * only characters (and endings) Windows allows in a file name.
     */
    static String relativePath(String what, String value) throws TemplateException {
        if (value.isEmpty()) {
            throw invalid(quoted(what) + " is empty");
        }
        if (value.startsWith("/")) {
            throw invalid(quoted(what) + " is \"" + value + "\", which is not a relative path");
        }
        for (String part : value.split("/", -1)) {
            if (part.isEmpty() || part.equals(".") || part.equals("..")) {
                throw invalid(quoted(what) + " is \"" + value + "\": use a relative path with '/' between folder"
                        + " names, without '.' or '..'");
            }
            for (int i = 0; i < part.length(); i++) {
                char c = part.charAt(i);
                if (Character.isISOControl(c) || FORBIDDEN_CHARACTERS.indexOf(c) >= 0) {
                    throw invalid(quoted(what) + " is \"" + value + "\", which holds a character Windows does not"
                            + " allow in a file name");
                }
            }
            char last = part.charAt(part.length() - 1);
            if (last == ' ' || last == '.') {
                throw invalid(quoted(what) + " is \"" + value + "\": a name may not end with a space or a dot");
            }
        }
        return value;
    }

    /** Whether a project-relative path is, or lies in, {@link #EDITOR_STATE_DIR}. */
    static boolean isEditorState(String projectRelative) {
        int slash = projectRelative.indexOf('/');
        String first = slash < 0 ? projectRelative : projectRelative.substring(0, slash);
        return first.equalsIgnoreCase(EDITOR_STATE_DIR);
    }

    private static String quoted(String what) {
        return what.startsWith("the ") ? what : "\"" + what + "\"";
    }

    private static TemplateException invalid(String reason) {
        return new TemplateException(FILE_NAME + " is not valid: " + reason);
    }

    private static JsonObject parseObject(String text) throws TemplateException {
        try {
            JsonReader reader = new JsonReader(new StringReader(text));
            // parseReader only relaxes a reader left at the default LEGACY_STRICT, so this stays strict.
            reader.setStrictness(Strictness.STRICT);
            JsonElement element = JsonParser.parseReader(reader);
            if (!element.isJsonObject()) {
                throw invalid(element.isJsonNull() ? "it is empty" : "its top level is not a JSON object");
            }
            if (reader.peek() != JsonToken.END_DOCUMENT) {
                throw invalid("there is more data after the JSON document");
            }
            return element.getAsJsonObject();
        } catch (JsonParseException | IOException e) {
            throw invalid("it is not valid JSON: " + e.getMessage());
        }
    }

    private static String requiredString(JsonObject object, String key) throws TemplateException {
        JsonElement element = object.get(key);
        if (element == null || !element.isJsonPrimitive() || !element.getAsJsonPrimitive().isString()) {
            throw invalid("\"" + key + "\" is missing or not a string");
        }
        return element.getAsString();
    }

    /** The member as a string; empty when it is absent or JSON {@code null}; an error when it is anything else. */
    private static String optionalString(JsonObject object, String key) throws TemplateException {
        JsonElement element = object.get(key);
        if (element == null || element.isJsonNull()) {
            return "";
        }
        if (!element.isJsonPrimitive() || !element.getAsJsonPrimitive().isString()) {
            throw invalid("\"" + key + "\" is not a string");
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
