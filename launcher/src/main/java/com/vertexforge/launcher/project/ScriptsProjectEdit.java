package com.vertexforge.launcher.project;

import com.vertexforge.launcher.template.Placeholders;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Objects;

/**
 * Gives a copied project's {@code scripts.mtproj} the copy's name (VK-1695): the {@code Name} attribute of its root
 * {@code <Project>} element, which a project creation fills from the template's {@code {{PROJECT_NAME}}} (VK-1689).
 * The engine uses that name only as a label (its library is always {@code compiled/scripts.mtcLib}), so a copy that
 * kept the old one would still build, but would carry the original's name.
 * <p>
 * The file is edited byte for byte: only the bytes between the attribute's double quotes change, so line endings, the
 * prolog, comments and every other attribute stay exactly as they were. It is read the way mType's own parser reads
 * it ({@code dependencies/mType/mType/project/XmlParser.cpp}, {@code ProjectConfigParser.cpp}): whitespace and
 * {@code <!-- -->} comments, an optional {@code <?xml … ?>} prolog, then the root element, whose tag must be
 * {@code Project}; an attribute name runs up to {@code =} or whitespace, whitespace may surround the {@code =}, only a
 * double-quoted value counts, and when a name repeats the last one wins. The new value is escaped as a project creation
 * escapes it ({@link Placeholders#xmlAttribute}). Pure.
 */
public final class ScriptsProjectEdit {

    /** A scripts project is a few hundred bytes; a larger file is left as it is rather than read whole. */
    public static final int MAX_BYTES = 1024 * 1024;

    private static final String ROOT_TAG = "Project";
    private static final String NAME_ATTRIBUTE = "Name";

    /** What {@link #rename} made of the file. */
    public sealed interface Result {
        /** The file with its {@code Name} replaced; every other byte is the original's. */
        record Renamed(byte[] bytes) implements Result {
            public Renamed {
                Objects.requireNonNull(bytes, "bytes");
            }
        }

        /** The file is copied as it is: {@code reason} says why its name could not be replaced. */
        record Unchanged(String reason) implements Result {
            public Unchanged {
                Objects.requireNonNull(reason, "reason");
            }
        }
    }

    private ScriptsProjectEdit() {
        // Utility class - prevent instantiation
    }

    /** Replaces the root {@code Name} of {@code original} with {@code newName}, or says why it cannot. */
    public static Result rename(byte[] original, String newName) {
        Objects.requireNonNull(original, "original");
        Objects.requireNonNull(newName, "newName");
        if (original.length > MAX_BYTES) {
            return new Result.Unchanged("it is larger than " + (MAX_BYTES / (1024 * 1024)) + " MiB");
        }
        if (original.length >= 2 && ((original[0] == (byte) 0xFF && original[1] == (byte) 0xFE)
                || (original[0] == (byte) 0xFE && original[1] == (byte) 0xFF))) {
            return new Result.Unchanged("it is UTF-16 text, which mType does not read");
        }

        int pos = startsWith(original, 0, new byte[] {(byte) 0xEF, (byte) 0xBB, (byte) 0xBF}) ? 3 : 0;
        pos = skipWhitespaceAndComments(original, pos);
        if (startsWith(original, pos, ascii("<?xml"))) {
            int end = indexOf(original, ascii("?>"), pos);
            if (end < 0) {
                return new Result.Unchanged("its <?xml … ?> prolog never ends");
            }
            pos = skipWhitespaceAndComments(original, end + 2);
        }
        if (pos >= original.length || original[pos] != '<') {
            return new Result.Unchanged("it does not start with an XML element");
        }
        pos++;

        int tagStart = pos;
        while (pos < original.length && !isSpace(original[pos]) && original[pos] != '>' && original[pos] != '/') {
            pos++;
        }
        String tag = new String(original, tagStart, pos - tagStart, StandardCharsets.UTF_8);
        if (!tag.equals(ROOT_TAG)) {
            return new Result.Unchanged("its root element is <" + tag + ">, not <" + ROOT_TAG + ">");
        }

        // The attribute loop of XmlParser::parseAttributes, remembering where the last Name's value is.
        int valueStart = -1;
        int valueEnd = -1;
        boolean terminated = false;
        while (pos < original.length) {
            pos = skipWhitespace(original, pos);
            if (pos >= original.length || original[pos] == '>' || original[pos] == '/') {
                break;
            }
            int nameStart = pos;
            while (pos < original.length && original[pos] != '=' && !isSpace(original[pos])) {
                pos++;
            }
            boolean isName = equalsAscii(original, nameStart, pos, NAME_ATTRIBUTE);
            pos = skipWhitespace(original, pos);
            if (pos >= original.length || original[pos] != '=') {
                continue;
            }
            pos++;
            pos = skipWhitespace(original, pos);
            if (pos >= original.length || original[pos] != '"') {
                continue;
            }
            pos++;
            int start = pos;
            while (pos < original.length && original[pos] != '"') {
                pos++;
            }
            boolean closed = pos < original.length;
            if (isName) {
                valueStart = start;
                valueEnd = pos;
                terminated = closed;
            }
            if (closed) {
                pos++;
            }
        }

        if (valueStart < 0) {
            return new Result.Unchanged("its <" + ROOT_TAG + "> has no " + NAME_ATTRIBUTE + "=\"…\" attribute");
        }
        if (!terminated) {
            return new Result.Unchanged("its " + NAME_ATTRIBUTE + " attribute's value never ends");
        }

        byte[] value = Placeholders.xmlAttribute(newName).getBytes(StandardCharsets.UTF_8);
        ByteArrayOutputStream out = new ByteArrayOutputStream(original.length + value.length);
        out.write(original, 0, valueStart);
        out.write(value, 0, value.length);
        out.write(original, valueEnd, original.length - valueEnd);
        return new Result.Renamed(out.toByteArray());
    }

    /** {@code std::isspace} in the "C" locale: space, tab, line feed, vertical tab, form feed, carriage return. */
    private static boolean isSpace(byte b) {
        return b == ' ' || b == '\t' || b == '\n' || b == 0x0B || b == '\f' || b == '\r';
    }

    private static int skipWhitespace(byte[] bytes, int pos) {
        while (pos < bytes.length && isSpace(bytes[pos])) {
            pos++;
        }
        return pos;
    }

    /** XmlParser::skipWhitespaceAndComments: an unterminated comment runs to the end of the file. */
    private static int skipWhitespaceAndComments(byte[] bytes, int pos) {
        for (;;) {
            pos = skipWhitespace(bytes, pos);
            if (pos + 3 >= bytes.length || !startsWith(bytes, pos, ascii("<!--"))) {
                return pos;
            }
            int end = indexOf(bytes, ascii("-->"), pos + 4);
            pos = end < 0 ? bytes.length : end + 3;
        }
    }

    private static boolean startsWith(byte[] bytes, int pos, byte[] prefix) {
        if (pos < 0 || pos + prefix.length > bytes.length) {
            return false;
        }
        for (int i = 0; i < prefix.length; i++) {
            if (bytes[pos + i] != prefix[i]) {
                return false;
            }
        }
        return true;
    }

    private static int indexOf(byte[] bytes, byte[] needle, int from) {
        for (int i = Math.max(from, 0); i + needle.length <= bytes.length; i++) {
            if (startsWith(bytes, i, needle)) {
                return i;
            }
        }
        return -1;
    }

    private static boolean equalsAscii(byte[] bytes, int start, int end, String text) {
        return end - start == text.length() && startsWith(bytes, start, ascii(text));
    }

    private static byte[] ascii(String text) {
        return text.getBytes(StandardCharsets.US_ASCII);
    }
}
