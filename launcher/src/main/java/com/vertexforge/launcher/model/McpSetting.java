package com.vertexforge.launcher.model;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;

import java.math.BigDecimal;
import java.util.OptionalInt;

/**
 * "Open with MCP" for one project (VK-1683): when enabled, the editor starts with {@code --mcp --mcp-port <port>}, so
 * an AI agent can connect to it. A token is never passed: a command line is visible to every process on the machine,
 * so the editor uses the token from its own preferences, if one is set.
 * <p>
 * Stored as the entry's {@code "mcp": {"enabled": true, "port": 7878}} member. Reading is forgiving: a missing or
 * malformed member reads as {@link #DEFAULT}, and a bad port as {@link #DEFAULT_PORT}.
 *
 * @param enabled whether the editor starts with its MCP server on
 * @param port    the port it listens on, from {@link #MIN_PORT} to {@link #MAX_PORT}
 */
public record McpSetting(boolean enabled, int port) {

    /** The editor's own default port ({@code EditorPreferences.hpp}). */
    public static final int DEFAULT_PORT = 7878;
    /** The editor's {@code --mcp-port} range ({@code launch::minMcpPort} / {@code maxMcpPort}). */
    public static final int MIN_PORT = 1024;
    public static final int MAX_PORT = 65535;

    public static final McpSetting DEFAULT = new McpSetting(false, DEFAULT_PORT);

    static final String ENABLED = "enabled";
    static final String PORT = "port";

    public McpSetting {
        if (!isValidPort(port)) {
            throw new IllegalArgumentException("port must be from " + MIN_PORT + " to " + MAX_PORT + ": " + port);
        }
    }

    public static boolean isValidPort(int port) {
        return port >= MIN_PORT && port <= MAX_PORT;
    }

    /** {@code "7878"} as a valid port; empty for anything else (signs, spaces, out of range). */
    public static OptionalInt parsePort(String text) {
        if (text == null || text.isEmpty() || text.length() > 5) {
            return OptionalInt.empty();
        }
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (c < '0' || c > '9') {
                return OptionalInt.empty();
            }
        }
        int port = Integer.parseInt(text);
        return isValidPort(port) ? OptionalInt.of(port) : OptionalInt.empty();
    }

    /** The stored member; {@link #DEFAULT} when it is missing or not an object, and the default port for a bad one. */
    public static McpSetting fromJson(JsonElement element) {
        if (element == null || !element.isJsonObject()) {
            return DEFAULT;
        }
        JsonObject object = element.getAsJsonObject();
        JsonElement enabled = object.get(ENABLED);
        boolean on = enabled != null && enabled.isJsonPrimitive() && enabled.getAsJsonPrimitive().isBoolean()
                && enabled.getAsBoolean();
        OptionalInt port = wholeNumber(object.get(PORT));
        return new McpSetting(on, port.isPresent() && isValidPort(port.getAsInt()) ? port.getAsInt() : DEFAULT_PORT);
    }

    public JsonObject toJson() {
        JsonObject object = new JsonObject();
        object.add(ENABLED, new JsonPrimitive(enabled));
        object.add(PORT, new JsonPrimitive(port));
        return object;
    }

    private static OptionalInt wholeNumber(JsonElement element) {
        if (element == null || !element.isJsonPrimitive() || !element.getAsJsonPrimitive().isNumber()) {
            return OptionalInt.empty();
        }
        try {
            BigDecimal value = element.getAsBigDecimal();
            return OptionalInt.of(value.intValueExact());
        } catch (ArithmeticException | NumberFormatException e) {
            return OptionalInt.empty();
        }
    }
}
