package com.vertexforge.launcher.scriptlib;

import com.google.gson.GsonBuilder;
import com.google.gson.JsonObject;
import com.google.gson.Strictness;
import com.google.gson.stream.JsonReader;
import com.google.gson.stream.JsonToken;

import java.io.IOException;
import java.io.StringReader;
import java.nio.charset.StandardCharsets;
import java.util.HashSet;
import java.util.Objects;
import java.util.Set;
import java.util.regex.Pattern;

/**
 * The note a script-library update leaves beside the library while it swaps folders (VK-1691):
 * {@code <wd>/scripts/.vflib-update.json}. It is written once, before the new library is built, and deleted when the
 * update is over; when it is still there, an update was interrupted, and {@link RecoveryPlanner} works out how to
 * finish or undo it from the folders it names, and from nothing else.
 *
 * @param formatVersion      {@link #FORMAT_VERSION}
 * @param id                 the update's id
 * @param library            the library folder's name ({@code lib}, as the project spells it)
 * @param staging            the folder the new library is built in ({@code lib.new-<8 hex>})
 * @param previous           the name the old library is moved to during the swap ({@code lib.old-<8 hex>})
 * @param hadLibrary         whether the project had a library folder before the update
 * @param newManifestSha256  the plain SHA-256 of the new library's {@code .vflib.json} bytes, written last: the new
 *                           library is complete exactly when its {@code .vflib.json} has this hash
 * @param backup             the backup zip, '/'-relative to the project folder; {@code null} when there was nothing to
 *                           back up
 * @param engineVersion      the engine version the update brings the library to
 * @param startedAt          when the update started, ISO-8601 UTC
 */
public record UpdateJournal(int formatVersion, String id, String library, String staging, String previous,
                            boolean hadLibrary, String newManifestSha256, String backup, String engineVersion,
                            String startedAt) {

    /** The journal's name, beside the library folder. */
    public static final String FILE_NAME = ".vflib-update.json";
    public static final int FORMAT_VERSION = 1;
    /** What the staging and previous folders' names add to the library's: {@code .new-} or {@code .old-}, then 8 hex. */
    public static final String STAGING_INFIX = ".new-";
    public static final String PREVIOUS_INFIX = ".old-";
    /** The library folder's name (in any letter case). */
    public static final String LIBRARY_FOLDER = "lib";
    /** Where an update's backups go, relative to the project folder. */
    public static final String BACKUP_FOLDER = ".vertexforge/backups";
    /** The start of every backup's name. */
    public static final String BACKUP_PREFIX = "scripts-lib-";

    private static final Pattern ID_SUFFIX = Pattern.compile("[0-9a-f]{8}");
    private static final Pattern SHA256 = Pattern.compile("[0-9a-f]{64}");

    /** A journal that cannot be used; {@link #newerFormat()} when a newer launcher wrote it. */
    public static final class InvalidJournalException extends Exception {
        private final boolean newerFormat;

        InvalidJournalException(String message, boolean newerFormat) {
            super(message);
            this.newerFormat = newerFormat;
        }

        public boolean newerFormat() {
            return newerFormat;
        }
    }

    public UpdateJournal {
        Objects.requireNonNull(id, "id");
        requireName(library, "library");
        requireName(staging, "staging");
        requireName(previous, "previous");
        Objects.requireNonNull(newManifestSha256, "newManifestSha256");
        Objects.requireNonNull(engineVersion, "engineVersion");
        Objects.requireNonNull(startedAt, "startedAt");
    }

    /** The staging folder's name for {@code library} and an update id's first 8 hex digits. */
    public static String stagingName(String library, String idSuffix) {
        return library + STAGING_INFIX + idSuffix;
    }

    /** The previous folder's name for {@code library} and an update id's first 8 hex digits. */
    public static String previousName(String library, String idSuffix) {
        return library + PREVIOUS_INFIX + idSuffix;
    }

    /**
     * Whether {@code name} looks like a staging or previous folder of {@code library} ({@code lib.new-1a2b3c4d},
     * ignoring case): such a folder without a journal is never touched, but it means the library needs a look.
     */
    public static boolean looksLikeUpdateFolder(String library, String name) {
        int length = library.length() + STAGING_INFIX.length();
        if (name.length() != length + 8 || !name.regionMatches(true, 0, library, 0, library.length())) {
            return false;
        }
        String infix = name.substring(library.length(), length);
        return (infix.equalsIgnoreCase(STAGING_INFIX) || infix.equalsIgnoreCase(PREVIOUS_INFIX))
                && ID_SUFFIX.matcher(name.substring(length).toLowerCase(java.util.Locale.ROOT)).matches();
    }

    /** The journal's bytes: UTF-8 JSON, LF line endings. */
    public byte[] toJson() {
        JsonObject root = new JsonObject();
        root.addProperty("formatVersion", formatVersion);
        root.addProperty("id", id);
        root.addProperty("library", library);
        root.addProperty("staging", staging);
        root.addProperty("previous", previous);
        root.addProperty("hadLibrary", hadLibrary);
        root.addProperty("newManifestSha256", newManifestSha256);
        if (backup != null) {
            root.addProperty("backup", backup);
        }
        root.addProperty("engineVersion", engineVersion);
        root.addProperty("startedAt", startedAt);
        String json = new GsonBuilder().setPrettyPrinting().disableHtmlEscaping().create().toJson(root) + "\n";
        return json.getBytes(StandardCharsets.UTF_8);
    }

    /**
     * Reads a journal, strictly. The folder names must be single names derived from the library's (so a damaged
     * journal can never point recovery at another folder), and the version is checked before anything else.
     */
    public static UpdateJournal parse(byte[] bytes) throws InvalidJournalException {
        Objects.requireNonNull(bytes, "bytes");
        String formatVersion = null;
        String id = null;
        String library = null;
        String staging = null;
        String previous = null;
        Boolean hadLibrary = null;
        String manifest = null;
        String backup = null;
        String engineVersion = null;
        String startedAt = null;
        try (JsonReader reader = new JsonReader(new StringReader(new String(bytes, StandardCharsets.UTF_8)))) {
            reader.setStrictness(Strictness.STRICT);
            reader.beginObject();
            Set<String> names = new HashSet<>();
            while (reader.hasNext()) {
                String name = reader.nextName();
                if (!names.add(name)) {
                    throw invalid("\"" + name + "\" appears twice");
                }
                switch (name) {
                    case "formatVersion" -> formatVersion = scalar(reader);
                    case "id" -> id = string(reader);
                    case "library" -> library = string(reader);
                    case "staging" -> staging = string(reader);
                    case "previous" -> previous = string(reader);
                    case "hadLibrary" -> {
                        if (reader.peek() == JsonToken.BOOLEAN) {
                            hadLibrary = reader.nextBoolean();
                        } else {
                            reader.skipValue();
                        }
                    }
                    case "newManifestSha256" -> manifest = string(reader);
                    case "backup" -> backup = string(reader);
                    case "engineVersion" -> engineVersion = string(reader);
                    case "startedAt" -> startedAt = string(reader);
                    default -> reader.skipValue();
                }
            }
            reader.endObject();
            if (reader.peek() != JsonToken.END_DOCUMENT) {
                throw invalid("there is more data after the JSON object");
            }
        } catch (IOException | IllegalStateException e) {
            throw invalid("it is not valid JSON: " + e.getMessage());
        }

        if (formatVersion == null || !formatVersion.matches("[1-9][0-9]{0,8}")) {
            throw invalid("\"formatVersion\" is missing or not a whole number");
        }
        int version = Integer.parseInt(formatVersion);
        if (version > FORMAT_VERSION) {
            throw new InvalidJournalException("it has format version " + version + ", newer than this launcher reads",
                    true);
        }
        if (id == null || library == null || staging == null || previous == null || hadLibrary == null
                || manifest == null || engineVersion == null || startedAt == null) {
            throw invalid("a member is missing or has the wrong type");
        }
        // Only names an update can have: so a damaged journal never makes recovery touch any other folder.
        if (!library.equalsIgnoreCase(LIBRARY_FOLDER) || !looksLikeUpdateFolder(library, staging)
                || !looksLikeUpdateFolder(library, previous)
                || !staging.regionMatches(true, library.length(), STAGING_INFIX, 0, STAGING_INFIX.length())
                || !previous.regionMatches(true, library.length(), PREVIOUS_INFIX, 0, PREVIOUS_INFIX.length())) {
            throw invalid("its folder names are not the library's own update folders");
        }
        if (!SHA256.matcher(manifest).matches()) {
            throw invalid("\"newManifestSha256\" is not 64 lowercase hex digits");
        }
        if (backup != null && !isBackupPath(backup)) {
            throw invalid("\"backup\" is not a backup of the project's state folder");
        }
        return new UpdateJournal(version, id, library, staging, previous, hadLibrary, manifest, backup, engineVersion,
                startedAt);
    }

    private static String scalar(JsonReader reader) throws IOException {
        JsonToken token = reader.peek();
        if (token == JsonToken.NUMBER || token == JsonToken.STRING) {
            return reader.nextString();
        }
        reader.skipValue();
        return null;
    }

    private static String string(JsonReader reader) throws IOException {
        if (reader.peek() == JsonToken.STRING) {
            return reader.nextString();
        }
        reader.skipValue();
        return null;
    }

    /**
     * Whether {@code path} is where an update puts its backups: '/'-relative to the project folder, in
     * {@link #BACKUP_FOLDER}, a {@link #BACKUP_PREFIX}{@code …}{@code .zip} file, and nothing a path cannot hold.
     */
    static boolean isBackupPath(String path) {
        if (!com.vertexforge.launcher.template.LibraryManifest.isValidPath(path)) {
            return false;
        }
        String name = path.substring(path.lastIndexOf('/') + 1);
        if (!path.regionMatches(true, 0, BACKUP_FOLDER + "/", 0, BACKUP_FOLDER.length() + 1)
                || path.indexOf('/', BACKUP_FOLDER.length() + 1) >= 0
                || !name.regionMatches(true, 0, BACKUP_PREFIX, 0, BACKUP_PREFIX.length())
                || !name.toLowerCase(java.util.Locale.ROOT).endsWith(".zip")) {
            return false;
        }
        for (char c : path.toCharArray()) {
            if (c < ' ' || "<>\"|?*".indexOf(c) >= 0) {
                return false;
            }
        }
        return true;
    }

    /** A single folder name: not empty, not {@code .} or {@code ..}, without a separator or a colon. */
    static boolean isName(String name) {
        return name != null && !name.isEmpty() && !name.equals(".") && !name.equals("..") && name.indexOf('/') < 0
                && name.indexOf('\\') < 0 && name.indexOf(':') < 0;
    }

    private static void requireName(String name, String what) {
        if (!isName(name)) {
            throw new IllegalArgumentException(what + " must be a single folder name: " + name);
        }
    }

    private static InvalidJournalException invalid(String message) {
        return new InvalidJournalException(message, false);
    }
}
