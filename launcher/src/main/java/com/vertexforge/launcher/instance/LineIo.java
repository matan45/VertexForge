package com.vertexforge.launcher.instance;

import java.io.ByteArrayOutputStream;
import java.io.EOFException;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.ReadableByteChannel;
import java.nio.channels.WritableByteChannel;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;

/**
 * Line-at-a-time I/O over a blocking channel for the single-instance protocol (VK-1685). The protocol takes strict
 * turns, one line each, so nothing ever follows a line within a turn; bytes after the line break in the same read
 * are dropped. A blocking channel has no read timeout: callers bound every exchange with a watchdog that closes the
 * channel.
 */
final class LineIo {

    private static final int CHUNK = 4096;

    /** A line longer than the limit; nothing more is read from the channel. */
    static final class LineTooLongException extends IOException {
        LineTooLongException(int maxBytes) {
            super("the line is longer than " + maxBytes + " bytes");
        }
    }

    private LineIo() {
        // Utility class - prevent instantiation
    }

    /** Writes {@code text} as UTF-8 followed by a line break, all of it. */
    static void writeLine(WritableByteChannel channel, String text) throws IOException {
        ByteBuffer buffer = ByteBuffer.wrap((text + "\n").getBytes(StandardCharsets.UTF_8));
        while (buffer.hasRemaining()) {
            channel.write(buffer);
        }
    }

    /**
     * Reads up to the next line break and decodes the line as strict UTF-8 (a trailing carriage return is dropped).
     *
     * @throws EOFException             when the other side closed before the line was complete
     * @throws LineTooLongException     when the line is longer than {@code maxBytes}
     * @throws CharacterCodingException when it is not valid UTF-8
     */
    static String readLine(ReadableByteChannel channel, int maxBytes) throws IOException {
        ByteArrayOutputStream line = new ByteArrayOutputStream();
        ByteBuffer buffer = ByteBuffer.allocate(CHUNK);
        while (true) {
            buffer.clear();
            int read = channel.read(buffer);
            if (read < 0) {
                throw new EOFException("the connection closed before a whole line arrived");
            }
            buffer.flip();
            while (buffer.hasRemaining()) {
                byte next = buffer.get();
                if (next == '\n') {
                    return decode(line.toByteArray());
                }
                if (line.size() >= maxBytes) {
                    throw new LineTooLongException(maxBytes);
                }
                line.write(next);
            }
        }
    }

    private static String decode(byte[] bytes) throws CharacterCodingException {
        int length = bytes.length;
        if (length > 0 && bytes[length - 1] == '\r') {
            length--;
        }
        return StandardCharsets.UTF_8.newDecoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .decode(ByteBuffer.wrap(bytes, 0, length))
                .toString();
    }
}
