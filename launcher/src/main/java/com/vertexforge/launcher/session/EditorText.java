package com.vertexforge.launcher.session;

import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.Charset;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;

/**
 * Text the editor wrote: its log and its crash reports. An editor of VK-1684 or later runs in the UTF-8 code page, so
 * all of it is UTF-8; an older one wrote paths in the system's ANSI code page. Each line is therefore decoded as strict
 * UTF-8 first and in the fallback charset when it is not valid UTF-8.
 */
final class EditorText {

    private EditorText() {
        // Utility class - prevent instantiation
    }

    /** {@code bytes[from, to)} as strict UTF-8, or in {@code fallback} when it is not valid UTF-8. */
    static String decode(byte[] bytes, int from, int to, Charset fallback) {
        ByteBuffer buffer = ByteBuffer.wrap(bytes, from, to - from);
        try {
            return StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(buffer)
                    .toString();
        } catch (CharacterCodingException e) {
            return new String(bytes, from, to - from, fallback);
        }
    }

    /**
     * The first {@code length} bytes, each line ({@code \n}-terminated, the last one possibly not) decoded on its own
     * by {@link #decode}, terminators kept. A line apart: a report cut inside a character, or one line in the code
     * page, leaves the other lines readable.
     */
    static String decodeLines(byte[] bytes, int length, Charset fallback) {
        StringBuilder out = new StringBuilder(length);
        int lineStart = 0;
        for (int i = 0; i < length; i++) {
            if (bytes[i] == '\n') {
                out.append(decode(bytes, lineStart, i + 1, fallback));
                lineStart = i + 1;
            }
        }
        if (lineStart < length) {
            out.append(decode(bytes, lineStart, length, fallback));
        }
        return out.toString();
    }
}
