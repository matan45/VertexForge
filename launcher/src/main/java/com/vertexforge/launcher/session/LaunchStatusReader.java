package com.vertexforge.launcher.session;

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
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Optional;

/**
 * Reads the editor's {@code --launch-status} file (VK-1683). The editor replaces the whole file at every step, and an
 * open reader blocks that replace, so the file is read with one {@link Files#readAllBytes} (open, read, close) and
 * never held open. A file that is absent, unreadable, half-written or not a status line means "no news".
 */
public final class LaunchStatusReader {

    private LaunchStatusReader() {
        // Utility class - prevent instantiation
    }

    /** The status in {@code file}; empty when there is none yet or it cannot be read. Blocks briefly. */
    public static Optional<LaunchStatus> read(Path file) {
        byte[] bytes;
        try {
            bytes = Files.readAllBytes(file);
        } catch (IOException | RuntimeException e) {
            return Optional.empty();
        }
        return parse(bytes);
    }

    /** The status in {@code file} when it was written by process {@code pid}; empty otherwise. */
    public static Optional<LaunchStatus> read(Path file, long pid) {
        return read(file).filter(status -> status.pid() == pid);
    }

    static Optional<LaunchStatus> parse(byte[] bytes) {
        String text;
        try {
            text = StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes))
                    .toString();
        } catch (CharacterCodingException e) {
            return Optional.empty();
        }
        return parse(text);
    }

    static Optional<LaunchStatus> parse(String text) {
        JsonObject root;
        try {
            JsonReader reader = new JsonReader(new StringReader(text));
            reader.setStrictness(Strictness.STRICT);
            JsonElement element = JsonParser.parseReader(reader);
            if (!element.isJsonObject() || reader.peek() != JsonToken.END_DOCUMENT) {
                return Optional.empty();
            }
            root = element.getAsJsonObject();
        } catch (JsonParseException | IOException | IllegalStateException e) {
            return Optional.empty();
        }

        Optional<LaunchStatus.State> state = string(root.get("state")).flatMap(LaunchStatus.State::fromJson);
        Optional<Long> code = wholeNumber(root.get("code"));
        Optional<String> message = string(root.get("message"));
        Optional<Long> pid = wholeNumber(root.get("pid"));
        if (state.isEmpty() || code.isEmpty() || message.isEmpty() || pid.isEmpty()
                || code.get() < Integer.MIN_VALUE || code.get() > Integer.MAX_VALUE || pid.get() < 0) {
            return Optional.empty();
        }
        return Optional.of(new LaunchStatus(state.get(), code.get().intValue(), message.get(), pid.get()));
    }

    private static Optional<String> string(JsonElement element) {
        if (element == null || !element.isJsonPrimitive() || !element.getAsJsonPrimitive().isString()) {
            return Optional.empty();
        }
        return Optional.of(element.getAsString());
    }

    private static Optional<Long> wholeNumber(JsonElement element) {
        if (element == null || !element.isJsonPrimitive()) {
            return Optional.empty();
        }
        JsonPrimitive primitive = element.getAsJsonPrimitive();
        if (!primitive.isNumber()) {
            return Optional.empty();
        }
        try {
            BigDecimal value = primitive.getAsBigDecimal();
            return Optional.of(value.longValueExact());
        } catch (ArithmeticException | NumberFormatException e) {
            return Optional.empty();
        }
    }
}
