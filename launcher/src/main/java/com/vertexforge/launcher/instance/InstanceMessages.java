package com.vertexforge.launcher.instance;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParseException;
import com.google.gson.JsonParser;
import com.google.gson.Strictness;
import com.google.gson.stream.JsonReader;
import com.google.gson.stream.JsonToken;
import com.vertexforge.launcher.instance.InstanceRequest.Action;

import java.io.IOException;
import java.io.StringReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Objects;
import java.util.regex.Pattern;

/**
 * The messages two launchers exchange over the single-instance socket (VK-1685). Each is one line of UTF-8 JSON
 * (Gson writes no line breaks and escapes those inside strings), at most {@link #MAX_LINE_BYTES} long:
 * <pre>
 *   running → new   {"app":"vertexforge-launcher","v":1,"pid":1234}
 *   new → running   {"v":1,"requests":[{"action":"open","path":"C:\\P\\Game.vfproj"}],"problems":[]}
 *   running → new   {"ok":true}   or   {"ok":false,"error":"closing"}
 * </pre>
 * Unknown members are ignored; {@code v} changes only for a change an older launcher cannot read.
 */
public final class InstanceMessages {

    public static final int VERSION = 1;
    public static final int MAX_LINE_BYTES = 64 * 1024;
    public static final String APP = "vertexforge-launcher";

    static final String APP_KEY = "app";
    static final String VERSION_KEY = "v";
    static final String PID_KEY = "pid";
    static final String REQUESTS_KEY = "requests";
    static final String PROBLEMS_KEY = "problems";
    static final String ACTION_KEY = "action";
    static final String PATH_KEY = "path";
    static final String OK_KEY = "ok";
    static final String ERROR_KEY = "error";

    /** HTML escaping off: paths stay readable in a log; the output is never embedded in HTML. */
    private static final Gson GSON = new GsonBuilder().disableHtmlEscaping().create();
    private static final Pattern WHOLE_NUMBER = Pattern.compile("0|[1-9]\\d{0,17}");

    private InstanceMessages() {
        // Utility class - prevent instantiation
    }

    /** A message that cannot be used; the text says why. */
    public static final class InvalidMessageException extends Exception {
        public InvalidMessageException(String message) {
            super(message);
        }
    }

    /** The running launcher's answer to a request. */
    public record Ack(boolean ok, String error) {
    }

    /** The first line the running launcher sends: who it is, and its process id for the foreground grant. */
    public static String greeting(long pid) {
        JsonObject root = new JsonObject();
        root.addProperty(APP_KEY, APP);
        root.addProperty(VERSION_KEY, VERSION);
        root.addProperty(PID_KEY, pid);
        return GSON.toJson(root);
    }

    /** The running launcher's process id from its greeting. */
    public static long parseGreeting(String line) throws InvalidMessageException {
        JsonObject root = parseObject(line);
        if (!APP.equals(string(root, APP_KEY))) {
            throw new InvalidMessageException("the other side is not a VertexForge launcher");
        }
        checkVersion(root);
        Long pid = wholeNumber(root.get(PID_KEY));
        if (pid == null || pid < 1) {
            throw new InvalidMessageException("the greeting has no valid \"" + PID_KEY + "\"");
        }
        return pid;
    }

    /** The request line a new launcher sends. */
    public static String request(InstanceMessage message) {
        Objects.requireNonNull(message, "message");
        JsonObject root = new JsonObject();
        root.addProperty(VERSION_KEY, VERSION);
        JsonArray requests = new JsonArray();
        for (InstanceRequest request : message.requests()) {
            JsonObject item = new JsonObject();
            item.addProperty(ACTION_KEY, request.action().name().toLowerCase(Locale.ROOT));
            item.addProperty(PATH_KEY, request.path().toString());
            requests.add(item);
        }
        root.add(REQUESTS_KEY, requests);
        JsonArray problems = new JsonArray();
        for (String problem : message.problems()) {
            problems.add(problem);
        }
        root.add(PROBLEMS_KEY, problems);
        return GSON.toJson(root);
    }

    /**
     * Reads a request line. Refused: a wrong version, {@code requests} missing or not a list, a request without a
     * known {@code action} or an absolute {@code path}, {@code problems} that are not a list of text, and a line over
     * {@link #MAX_LINE_BYTES}.
     */
    public static InstanceMessage parseRequest(String line) throws InvalidMessageException {
        JsonObject root = parseObject(line);
        checkVersion(root);

        JsonElement requestsElement = root.get(REQUESTS_KEY);
        if (requestsElement == null || !requestsElement.isJsonArray()) {
            throw new InvalidMessageException("\"" + REQUESTS_KEY + "\" is not a list");
        }
        List<InstanceRequest> requests = new ArrayList<>();
        for (JsonElement element : requestsElement.getAsJsonArray()) {
            if (!element.isJsonObject()) {
                throw new InvalidMessageException("a request is not a JSON object");
            }
            JsonObject item = element.getAsJsonObject();
            Action action = action(string(item, ACTION_KEY));
            requests.add(new InstanceRequest(action, absolutePath(string(item, PATH_KEY))));
        }

        List<String> problems = new ArrayList<>();
        JsonElement problemsElement = root.get(PROBLEMS_KEY);
        if (problemsElement != null && !problemsElement.isJsonNull()) {
            if (!problemsElement.isJsonArray()) {
                throw new InvalidMessageException("\"" + PROBLEMS_KEY + "\" is not a list");
            }
            for (JsonElement element : problemsElement.getAsJsonArray()) {
                if (!isString(element)) {
                    throw new InvalidMessageException("a problem is not text");
                }
                problems.add(element.getAsString());
            }
        }
        return new InstanceMessage(requests, problems);
    }

    /** The request was taken. */
    public static String ack() {
        JsonObject root = new JsonObject();
        root.addProperty(OK_KEY, true);
        return GSON.toJson(root);
    }

    /** The request was not taken, and why. */
    public static String refusal(String error) {
        Objects.requireNonNull(error, "error");
        JsonObject root = new JsonObject();
        root.addProperty(OK_KEY, false);
        root.addProperty(ERROR_KEY, error);
        return GSON.toJson(root);
    }

    /** Reads an answer; {@code error} is {@code null} when the line gives none. */
    public static Ack parseAck(String line) throws InvalidMessageException {
        JsonObject root = parseObject(line);
        JsonElement ok = root.get(OK_KEY);
        if (ok == null || !ok.isJsonPrimitive() || !ok.getAsJsonPrimitive().isBoolean()) {
            throw new InvalidMessageException("the answer has no \"" + OK_KEY + "\"");
        }
        return new Ack(ok.getAsBoolean(), string(root, ERROR_KEY));
    }

    private static JsonObject parseObject(String line) throws InvalidMessageException {
        if (line == null) {
            throw new InvalidMessageException("nothing was received");
        }
        if (line.getBytes(StandardCharsets.UTF_8).length > MAX_LINE_BYTES) {
            throw new InvalidMessageException("the message is longer than " + MAX_LINE_BYTES + " bytes");
        }
        JsonElement element;
        try {
            JsonReader reader = new JsonReader(new StringReader(line));
            // parseReader only relaxes a reader left at the default LEGACY_STRICT, so this stays strict.
            reader.setStrictness(Strictness.STRICT);
            element = JsonParser.parseReader(reader);
            if (reader.peek() != JsonToken.END_DOCUMENT) {
                throw new InvalidMessageException("there is more data after the message");
            }
        } catch (JsonParseException | IOException e) {
            throw new InvalidMessageException("the message is not valid JSON: " + reason(e));
        }
        if (!element.isJsonObject()) {
            throw new InvalidMessageException("the message is not a JSON object");
        }
        return element.getAsJsonObject();
    }

    private static void checkVersion(JsonObject root) throws InvalidMessageException {
        Long version = wholeNumber(root.get(VERSION_KEY));
        if (version == null) {
            throw new InvalidMessageException("the message has no version");
        }
        if (version != VERSION) {
            throw new InvalidMessageException("the message uses version " + version + ", but this launcher speaks"
                    + " version " + VERSION);
        }
    }

    private static Action action(String text) throws InvalidMessageException {
        if (text == null) {
            throw new InvalidMessageException("a request has no \"" + ACTION_KEY + "\"");
        }
        return switch (text) {
            case "add" -> Action.ADD;
            case "open" -> Action.OPEN;
            default -> throw new InvalidMessageException("a request has the unknown action \"" + text + "\"");
        };
    }

    private static Path absolutePath(String text) throws InvalidMessageException {
        if (text == null) {
            throw new InvalidMessageException("a request has no \"" + PATH_KEY + "\"");
        }
        Path path;
        try {
            path = Path.of(text);
        } catch (InvalidPathException e) {
            throw new InvalidMessageException("\"" + text + "\" is not a valid path");
        }
        if (!path.isAbsolute()) {
            throw new InvalidMessageException("\"" + text + "\" is not an absolute path");
        }
        return path;
    }

    /** A whole number written without a fraction or exponent; {@code null} otherwise. */
    private static Long wholeNumber(JsonElement element) {
        if (element == null || !element.isJsonPrimitive() || !element.getAsJsonPrimitive().isNumber()) {
            return null;
        }
        String text = element.getAsString();
        return WHOLE_NUMBER.matcher(text).matches() ? Long.parseLong(text) : null;
    }

    private static boolean isString(JsonElement element) {
        return element != null && element.isJsonPrimitive() && element.getAsJsonPrimitive().isString();
    }

    /** The member as a string; {@code null} when it is absent, JSON {@code null} or not a string. */
    private static String string(JsonObject object, String key) {
        JsonElement element = object.get(key);
        return isString(element) ? element.getAsString() : null;
    }

    /** Gson's own reason (its wrapper repeats the cause's class name), first line only (it adds a help link). */
    private static String reason(Exception e) {
        Throwable source = e.getCause() != null && e.getCause().getMessage() != null ? e.getCause() : e;
        String message = source.getMessage();
        if (message == null || message.isBlank()) {
            return source.getClass().getSimpleName();
        }
        int lineBreak = message.indexOf('\n');
        return lineBreak < 0 ? message : message.substring(0, lineBreak);
    }
}
