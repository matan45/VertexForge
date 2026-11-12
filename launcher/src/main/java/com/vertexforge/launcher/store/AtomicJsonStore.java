package com.vertexforge.launcher.store;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParseException;
import com.google.gson.JsonParser;
import com.google.gson.JsonPrimitive;
import com.google.gson.Strictness;
import com.google.gson.stream.JsonReader;
import com.google.gson.stream.JsonToken;
import com.vertexforge.launcher.store.StoreWriteException.Reason;

import java.io.IOException;
import java.io.InterruptedIOException;
import java.io.StringReader;
import java.math.BigDecimal;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.StandardCharsets;
import java.nio.file.AccessDeniedException;
import java.nio.file.DirectoryStream;
import java.nio.file.FileAlreadyExistsException;
import java.nio.file.FileSystemException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Clock;
import java.time.Duration;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.OptionalInt;
import java.util.function.Consumer;
import java.util.function.UnaryOperator;

/**
 * One JSON document in a file that several launcher processes share, written so that it is never lost (VK-1678).
 * The store knows nothing about what the document holds; the recent-projects list is its first client and the
 * engine registry (VK-1681) its next.
 * <ul>
 *   <li><b>Atomic writes</b>: the new content goes to a temp file in the same folder, is flushed to disk, and then
 *       replaces the file with {@code ATOMIC_MOVE}. A failure at any step leaves the previous file as it was.</li>
 *   <li><b>No lost updates</b>: every {@link #update} takes the locks in {@link FileLocks}, re-reads the file and
 *       applies its change to what it just read, so two launchers adding at the same time keep both changes.</li>
 *   <li><b>Corrupt files are kept</b>: a file that is not valid is moved aside to
 *       {@code <name>.corrupt-<UTC timestamp>} (never overwritten), and the list starts again empty.</li>
 *   <li><b>Newer files are never written</b>: a {@code "version"} above {@link DocumentFormat#currentVersion()}
 *       makes the document read-only. It is never quarantined, whatever its shape.</li>
 *   <li><b>Unknown members survive</b>: callers change the document they are given, so members this build does
 *       not know (including {@code null} ones) are written back unchanged.</li>
 * </ul>
 * Readers never block a replace: Java opens files on Windows with {@code FILE_SHARE_DELETE}.
 */
public final class AtomicJsonStore {

    public static final Duration DEFAULT_LOCK_TIMEOUT = Duration.ofSeconds(5);
    static final Duration DEFAULT_RETRY_DELAY = Duration.ofMillis(200);
    /** How many times the final move is tried; antivirus scanners briefly hold a just-written file. */
    static final int MOVE_ATTEMPTS = 5;
    static final String LOCK_SUFFIX = ".lock";
    static final String TEMP_SUFFIX = ".tmp";
    static final String CORRUPT_INFIX = ".corrupt-";

    private static final DateTimeFormatter QUARANTINE_STAMP =
            DateTimeFormatter.ofPattern("yyyyMMdd'T'HHmmss'Z'").withZone(ZoneOffset.UTC);
    private static final int MAX_QUARANTINE_NAMES = 1000;

    private final Path file;
    private final Path lockFile;
    private final DocumentFormat format;
    private final Clock clock;
    private final Duration lockTimeout;
    private final Consumer<String> warn;
    private final AtomicMover mover;
    private final Duration retryDelay;
    private final Gson gson;

    /**
     * @param warn receives every problem the store recovers from on its own (a quarantine, a leftover temp file it
     *             could not delete), normally {@code LauncherLog::warn}. Failed writes are thrown instead.
     */
    public AtomicJsonStore(Path file, DocumentFormat format, Clock clock, Duration lockTimeout, Consumer<String> warn) {
        this(file, format, clock, lockTimeout, warn, AtomicMover.SYSTEM, DEFAULT_RETRY_DELAY);
    }

    AtomicJsonStore(Path file, DocumentFormat format, Clock clock, Duration lockTimeout, Consumer<String> warn,
                    AtomicMover mover, Duration retryDelay) {
        this.file = Objects.requireNonNull(file, "file").toAbsolutePath().normalize();
        this.lockFile = this.file.resolveSibling(this.file.getFileName() + LOCK_SUFFIX);
        this.format = Objects.requireNonNull(format, "format");
        this.clock = Objects.requireNonNull(clock, "clock");
        this.lockTimeout = Objects.requireNonNull(lockTimeout, "lockTimeout");
        this.warn = Objects.requireNonNull(warn, "warn");
        this.mover = Objects.requireNonNull(mover, "mover");
        this.retryDelay = Objects.requireNonNull(retryDelay, "retryDelay");
        // serializeNulls: without it Gson drops every "x": null member, which would lose unknown members.
        this.gson = new GsonBuilder()
                .setPrettyPrinting()
                .disableHtmlEscaping()
                .serializeNulls()
                .create();
    }

    public Path file() {
        return file;
    }

    public Path lockFile() {
        return lockFile;
    }

    /**
     * Reads the file without taking a lock and without ever changing anything on disk. A corrupt file is reported as
     * {@link StoreStatus#CORRUPT} and left where it is. Never throws.
     */
    public StoreSnapshot inspect() {
        return parse().toSnapshot();
    }

    /**
     * Reads the file. A corrupt file is moved aside under the lock (after checking it is still corrupt) and reported
     * as {@link StoreStatus#CORRUPT_QUARANTINED}; when it cannot be moved, the result is
     * {@link StoreStatus#CORRUPT_UNMOVABLE}. Never throws, except {@link IllegalStateException} when called from
     * inside an {@link #update} of this file on the same thread.
     */
    public StoreSnapshot read() {
        Parsed parsed = parse();
        if (parsed.kind != Kind.CORRUPT) {
            return parsed.toSnapshot();
        }
        try (FileLocks.Held ignored = FileLocks.acquire(lockFile, lockTimeout)) {
            Parsed again = parse();
            if (again.kind != Kind.CORRUPT) {
                return again.toSnapshot();
            }
            try {
                Path movedTo = quarantineHeld(again.detail);
                return new StoreSnapshot(StoreStatus.CORRUPT_QUARANTINED, null, 0, Optional.of(movedTo), again.detail);
            } catch (IOException e) {
                return unmovable(again.detail, FileLocks.describe(e));
            }
        } catch (StoreWriteException e) {
            return unmovable(parsed.detail, e.getMessage());
        }
    }

    /**
     * Applies {@code mutation} to the current document and writes the result atomically. The mutation receives a
     * private copy of what the file holds right now (an empty object when there is no file), runs while both locks
     * are held, and must not call back into this store. The store then sets {@code "version"} to the current
     * version. When the result equals the file's content and the file already has the current version, nothing is
     * written.
     *
     * @return the document as written; its status is {@link StoreStatus#CORRUPT_QUARANTINED} when this update first
     * had to move a corrupt file aside, else {@link StoreStatus#OK}
     * @throws StoreWriteException when nothing was written; the previous file is then unchanged
     */
    public StoreSnapshot update(UnaryOperator<JsonObject> mutation) throws StoreWriteException {
        Objects.requireNonNull(mutation, "mutation");
        Path folder = file.getParent();
        try {
            Files.createDirectories(folder);
        } catch (IOException e) {
            throw new StoreWriteException(Reason.IO,
                    "Could not create the folder " + folder + ": " + FileLocks.describe(e), e);
        }

        try (FileLocks.Held ignored = FileLocks.acquire(lockFile, lockTimeout)) {
            deleteLeftoverTempFiles();

            Parsed current = parse();
            Optional<Path> quarantined = Optional.empty();
            JsonObject base;
            switch (current.kind) {
                case NEWER -> throw new StoreWriteException(Reason.NEWER_VERSION,
                        file.getFileName() + " was written by a newer version of the launcher (version "
                                + current.version + "); this version never overwrites it", null);
                case UNREADABLE -> throw new StoreWriteException(Reason.UNREADABLE,
                        "Could not read " + file + ": " + current.detail, null);
                case CORRUPT -> {
                    try {
                        quarantined = Optional.of(quarantineHeld(current.detail));
                    } catch (IOException e) {
                        throw new StoreWriteException(Reason.CORRUPT_UNMOVABLE, file + " is corrupt ("
                                + current.detail + ") and could not be moved aside: " + FileLocks.describe(e), e);
                    }
                    base = new JsonObject();
                }
                case ABSENT -> base = new JsonObject();
                default -> base = current.root;
            }

            JsonObject result = Objects.requireNonNull(mutation.apply(base.deepCopy()), "the mutation returned null");
            result = withCurrentVersion(result);

            boolean unchanged = current.kind == Kind.OK && result.equals(current.root);
            if (!unchanged) {
                try {
                    writeAtomically(result);
                } catch (StoreWriteException e) {
                    if (quarantined.isEmpty()) {
                        throw e;
                    }
                    // The corrupt file was already moved aside: the caller must still learn where.
                    throw new StoreWriteException(e.reason(), e.getMessage(), e.getCause(), quarantined.get());
                }
            }
            StoreStatus status = quarantined.isPresent() ? StoreStatus.CORRUPT_QUARANTINED : StoreStatus.OK;
            return new StoreSnapshot(status, result, format.currentVersion(), quarantined,
                    quarantined.isPresent() ? current.detail : "");
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Reading
    // ---------------------------------------------------------------------------------------------------------------

    private enum Kind { ABSENT, OK, LEGACY, NEWER, CORRUPT, UNREADABLE }

    /** One look at the file. Takes no lock and never writes. */
    private record Parsed(Kind kind, JsonObject root, int version, String detail) {

        static Parsed of(Kind kind, String detail) {
            return new Parsed(kind, new JsonObject(), 0, detail);
        }

        StoreSnapshot toSnapshot() {
            StoreStatus status = switch (kind) {
                case ABSENT -> StoreStatus.ABSENT;
                case OK -> StoreStatus.OK;
                case LEGACY -> StoreStatus.LEGACY;
                case NEWER -> StoreStatus.NEWER_READ_ONLY;
                case CORRUPT -> StoreStatus.CORRUPT;
                case UNREADABLE -> StoreStatus.UNREADABLE;
            };
            return new StoreSnapshot(status, root, version, Optional.empty(), detail);
        }
    }

    private Parsed parse() {
        String text;
        try {
            text = Files.readString(file, StandardCharsets.UTF_8);
        } catch (NoSuchFileException e) {
            return Parsed.of(Kind.ABSENT, "");
        } catch (CharacterCodingException e) {
            return Parsed.of(Kind.CORRUPT, "it is not valid UTF-8 text");
        } catch (IOException e) {
            return Parsed.of(Kind.UNREADABLE, FileLocks.describe(e));
        }

        JsonElement element;
        try {
            JsonReader reader = new JsonReader(new StringReader(text));
            // parseReader only relaxes a reader left at the default LEGACY_STRICT, so this stays strict.
            reader.setStrictness(Strictness.STRICT);
            element = JsonParser.parseReader(reader);
            if (!element.isJsonObject()) {
                return Parsed.of(Kind.CORRUPT, element.isJsonNull()
                        ? "it is empty or holds only null"
                        : "its top level is not a JSON object");
            }
            if (reader.peek() != JsonToken.END_DOCUMENT) {
                return Parsed.of(Kind.CORRUPT, "there is more data after the JSON document");
            }
        } catch (JsonParseException | IOException e) {
            return Parsed.of(Kind.CORRUPT, "it is not valid JSON: " + e.getMessage());
        }

        JsonObject root = element.getAsJsonObject();
        JsonElement versionElement = root.get(DocumentFormat.VERSION_KEY);
        int version;
        if (versionElement == null || versionElement.isJsonNull()) {
            OptionalInt assumed = format.versionWhenAbsent();
            if (assumed.isEmpty()) {
                return Parsed.of(Kind.CORRUPT, "it has no \"version\"");
            }
            version = assumed.getAsInt();
        } else {
            OptionalInt declared = wholeNumber(versionElement);
            if (declared.isEmpty() || declared.getAsInt() < 1) {
                return Parsed.of(Kind.CORRUPT, "its \"version\" is not a whole number of at least 1");
            }
            version = declared.getAsInt();
        }

        // Version first: a newer file is never structure-checked, so it can never be quarantined.
        if (version > format.currentVersion()) {
            return new Parsed(Kind.NEWER, root, version, "it was written by a newer version (" + version + ")");
        }
        try {
            format.structureCheck().check(root);
        } catch (CorruptDocumentException e) {
            return Parsed.of(Kind.CORRUPT, e.getMessage());
        }
        return new Parsed(version < format.currentVersion() ? Kind.LEGACY : Kind.OK, root, version, "");
    }

    private static OptionalInt wholeNumber(JsonElement element) {
        if (!element.isJsonPrimitive() || !element.getAsJsonPrimitive().isNumber()) {
            return OptionalInt.empty();
        }
        try {
            BigDecimal value = element.getAsBigDecimal();
            return OptionalInt.of(value.intValueExact());
        } catch (ArithmeticException | NumberFormatException e) {
            return OptionalInt.empty();
        }
    }

    private StoreSnapshot unmovable(String corruptDetail, String why) {
        String detail = corruptDetail + "; it could not be moved aside: " + why;
        warn.accept("The file " + file + " is corrupt (" + corruptDetail + ") and could not be moved aside ("
                + why + "). It is read-only until it is fixed or removed.");
        return new StoreSnapshot(StoreStatus.CORRUPT_UNMOVABLE, null, 0, Optional.empty(), detail);
    }

    /** Moves the corrupt file to a name that does not exist yet. The caller holds the lock. */
    private Path quarantineHeld(String detail) throws IOException {
        String base = file.getFileName() + CORRUPT_INFIX + QUARANTINE_STAMP.format(clock.instant());
        for (int n = 0; n < MAX_QUARANTINE_NAMES; n++) {
            Path target = file.resolveSibling(n == 0 ? base : base + "-" + n);
            if (Files.exists(target, LinkOption.NOFOLLOW_LINKS)) {
                continue;
            }
            try {
                // No options: a plain move never replaces an existing file.
                Files.move(file, target);
            } catch (FileAlreadyExistsException e) {
                continue;
            }
            warn.accept("The file " + file + " was corrupt (" + detail + "). It was kept as "
                    + target.getFileName() + " and a new, empty document was started.");
            return target;
        }
        throw new IOException("no free name for the corrupt copy of " + file);
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Writing
    // ---------------------------------------------------------------------------------------------------------------

    /** Sets {@code "version"}; a document that had none gets it as its first member. */
    private JsonObject withCurrentVersion(JsonObject document) {
        JsonPrimitive version = new JsonPrimitive(format.currentVersion());
        if (document.has(DocumentFormat.VERSION_KEY)) {
            document.add(DocumentFormat.VERSION_KEY, version);
            return document;
        }
        JsonObject ordered = new JsonObject();
        ordered.add(DocumentFormat.VERSION_KEY, version);
        for (Map.Entry<String, JsonElement> member : document.entrySet()) {
            ordered.add(member.getKey(), member.getValue());
        }
        return ordered;
    }

    private void writeAtomically(JsonObject document) throws StoreWriteException {
        byte[] bytes = (gson.toJson(document) + "\n").getBytes(StandardCharsets.UTF_8);
        Path folder = file.getParent();

        Path temp;
        try {
            temp = Files.createTempFile(folder, file.getFileName() + ".", TEMP_SUFFIX);
        } catch (IOException e) {
            throw new StoreWriteException(Reason.IO,
                    "Could not create a temporary file in " + folder + ": " + FileLocks.describe(e), e);
        }

        boolean moved = false;
        try {
            try (FileChannel channel = FileChannel.open(temp, StandardOpenOption.WRITE,
                    StandardOpenOption.TRUNCATE_EXISTING)) {
                ByteBuffer buffer = ByteBuffer.wrap(bytes);
                while (buffer.hasRemaining()) {
                    channel.write(buffer);
                }
                // On disk before the rename, so a power loss can never leave the new name with old or no data.
                channel.force(true);
            }
            moveWithRetry(temp);
            moved = true;
        } catch (IOException e) {
            throw new StoreWriteException(Reason.IO, "Could not save " + file + ": " + FileLocks.describe(e), e);
        } finally {
            if (!moved) {
                deleteQuietly(temp);
            }
        }
    }

    private void moveWithRetry(Path temp) throws IOException {
        for (int attempt = 1; ; attempt++) {
            try {
                mover.move(temp, file);
                return;
            } catch (IOException e) {
                if (attempt >= MOVE_ATTEMPTS || !isTransient(e)) {
                    throw e;
                }
            }
            try {
                Thread.sleep(retryDelay.toMillis());
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                InterruptedIOException interrupted = new InterruptedIOException("interrupted while saving " + file);
                interrupted.initCause(e);
                throw interrupted;
            }
        }
    }

    /**
     * A failure that a retry can fix: access denied, or a plain {@link FileSystemException}, which is how a Windows
     * sharing violation (another program, typically an antivirus scanner, has the file open) arrives. Subclasses such
     * as {@code AtomicMoveNotSupportedException} are never retried. The reason text is localized, so it is not read.
     */
    static boolean isTransient(IOException e) {
        return e instanceof AccessDeniedException || e.getClass() == FileSystemException.class;
    }

    /**
     * Deletes temp files a crashed write left behind. Only the lock holder creates them, so any that exist now are
     * stale. Only names {@code Files.createTempFile} produces for this store ({@code <name>.<digits>.tmp}) match.
     */
    private void deleteLeftoverTempFiles() {
        String prefix = file.getFileName() + ".";
        DirectoryStream.Filter<Path> leftover = entry -> {
            String name = entry.getFileName().toString();
            if (!name.startsWith(prefix) || !name.endsWith(TEMP_SUFFIX)
                    || name.length() <= prefix.length() + TEMP_SUFFIX.length()) {
                return false;
            }
            String middle = name.substring(prefix.length(), name.length() - TEMP_SUFFIX.length());
            return middle.chars().allMatch(Character::isDigit);
        };
        try (DirectoryStream<Path> stream = Files.newDirectoryStream(file.getParent(), leftover)) {
            for (Path stale : stream) {
                try {
                    Files.deleteIfExists(stale);
                } catch (IOException e) {
                    warn.accept("Could not delete the leftover temporary file " + stale + ": " + FileLocks.describe(e));
                }
            }
        } catch (IOException e) {
            warn.accept("Could not look for leftover temporary files next to " + file + ": " + FileLocks.describe(e));
        }
    }

    private static void deleteQuietly(Path path) {
        try {
            Files.deleteIfExists(path);
        } catch (IOException ignored) {
            // The next update deletes it under the lock.
        }
    }
}
