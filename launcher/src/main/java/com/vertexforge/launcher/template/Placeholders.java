package com.vertexforge.launcher.template;

import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;

/**
 * The placeholder a template's files carry and how it is filled in (VK-1689). Only the template's own
 * {@code .mtproj} and {@code .mt} files are filled; every other file is copied byte for byte.
 * <ul>
 *   <li>{@code .mtproj}: the value of a double-quoted XML attribute ({@code Name="{{PROJECT_NAME}}"}).</li>
 *   <li>{@code .mt}: the inside of an mType string literal ({@code "Hello from {{PROJECT_NAME}}!"}).</li>
 * </ul>
 * {@code VFEngine/tests/test_project_templates.cpp} holds the C++ twins of both escapes and checks the engine's
 * templates against them.
 */
public final class Placeholders {

    /** Inert as it is in XML, mType, JSON and {@code .gitignore}, and never found in the script library. */
    public static final String PROJECT_NAME = "{{PROJECT_NAME}}";

    /** How a file's placeholders are filled in. */
    public enum Fill {
        /** Copied byte for byte. */
        NONE,
        /** A double-quoted XML attribute value. */
        XML_ATTRIBUTE,
        /** The inside of an mType string literal. */
        MTYPE_STRING
    }

    private Placeholders() {
        // Utility class - prevent instantiation
    }

    /** How a template file called {@code fileName} is filled: by its extension, ignoring case. */
    public static Fill fillFor(String fileName) {
        String lower = fileName.toLowerCase(java.util.Locale.ROOT);
        if (lower.endsWith(".mtproj")) {
            return Fill.XML_ATTRIBUTE;
        }
        if (lower.endsWith(".mt")) {
            return Fill.MTYPE_STRING;
        }
        return Fill.NONE;
    }

    /**
     * Escapes a value for a double-quoted XML attribute. {@code '} needs no escape there and is left as it is: mType's
     * mtproj parser does not decode entities, so every escape it does not need would reach it as literal text.
     */
    public static String xmlAttribute(String value) {
        StringBuilder escaped = new StringBuilder(value.length());
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            switch (c) {
                case '&' -> escaped.append("&amp;");
                case '<' -> escaped.append("&lt;");
                case '>' -> escaped.append("&gt;");
                case '"' -> escaped.append("&quot;");
                default -> escaped.append(c);
            }
        }
        return escaped.toString();
    }

    /**
     * Escapes a value for the inside of an mType string literal: a backslash before each {@code \ " { }}. The lexer
     * decodes {@code \\ \" \{ \}} back to the character in both a plain {@code "…"} literal and an interpolated
     * {@code $"…"} one, where a bare opening brace would start an expression. Every other character, non-ASCII included,
     * stays as it is.
     */
    public static String mtypeString(String value) {
        StringBuilder escaped = new StringBuilder(value.length());
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            if (c == '\\' || c == '"' || c == '{' || c == '}') {
                escaped.append('\\');
            }
            escaped.append(c);
        }
        return escaped.toString();
    }

    /**
     * The file's bytes with every placeholder replaced by the escaped project name. A file without the placeholder (or
     * {@link Fill#NONE}) comes back as it is, byte for byte.
     *
     * @throws CharacterCodingException when a file to fill is not valid UTF-8
     */
    public static byte[] fill(Fill fill, byte[] bytes, String projectName) throws CharacterCodingException {
        if (fill == Fill.NONE) {
            return bytes;
        }
        String text = StandardCharsets.UTF_8.newDecoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .decode(ByteBuffer.wrap(bytes))
                .toString();
        if (!text.contains(PROJECT_NAME)) {
            return bytes;
        }
        String value = fill == Fill.XML_ATTRIBUTE ? xmlAttribute(projectName) : mtypeString(projectName);
        // A plain replace: the placeholder's braces are not regular-expression syntax here.
        return text.replace(PROJECT_NAME, value).getBytes(StandardCharsets.UTF_8);
    }

    /** Whether {@code bytes} are valid UTF-8. */
    public static boolean isUtf8(byte[] bytes) {
        try {
            StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes));
            return true;
        } catch (CharacterCodingException e) {
            return false;
        }
    }
}
