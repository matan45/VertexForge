package com.vertexforge.launcher.scriptlib;

import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.vertexforge.launcher.template.LibraryManifest;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.SortedMap;
import java.util.SortedSet;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Builds {@link LegacyLibraryHashes#RESOURCE} from the git history (VK-1691), with the production hash rule
 * ({@link LibraryManifest#hash}). The set holds every (path as installed below {@code scripts/lib}, hash) pair of:
 * <ul>
 *   <li>every revision of {@code launcher/src/main/resources/templates/liz.zip}, its {@code lib/} prefix stripped, as
 *       the launchers of January to May 2026 extracted it; and the first revision ({@link #FIRST_LAUNCHER_ZIP}) also
 *       unstripped, as the very first launcher (until 6871040ab) put it one folder too deep;</li>
 *   <li>every revision of {@code launcher/src/main/resources/templates/lib.zip} (May to November 2026);</li>
 *   <li>every blob of every commit of {@code assets/scripts/lib}: the launchers of November 2026 packed the working
 *       tree at build time, and users copied the folder by hand as the MCP primer told them to.</li>
 * </ul>
 * Everything is taken up to {@link #THROUGH}, the commit that brought in {@code .vflib.json}: every later project
 * records its own baseline, so the set never grows. Folders, {@code .vfmeta} sidecars and {@code .vflib.json} are left
 * out.
 */
final class LegacyLibraryHashesGenerator {

    /** VK-1689's commit: the last one before every new project carried its own {@code .vflib.json}. */
    static final String THROUGH = "9fdd282afb26bc403270a1366af7c482278a5447";
    /** The first {@code liz.zip}, which the first launcher extracted without stripping its {@code lib/} prefix. */
    static final String FIRST_LAUNCHER_ZIP = "9ff348d220d32b8e2e785638e7df58fe265e048f";

    static final String LIZ_ZIP = "launcher/src/main/resources/templates/liz.zip";
    static final String LIB_ZIP = "launcher/src/main/resources/templates/lib.zip";
    static final String ENGINE_LIBRARY = "assets/scripts/lib";
    private static final String PREFIX = "lib/";

    private LegacyLibraryHashesGenerator() {
    }

    /** Whether {@code repo} is a git checkout that holds {@link #THROUGH} (a shallow clone does not). */
    static boolean historyAvailable(Path repo) {
        try {
            return run(repo, null, "cat-file", "-e", THROUGH + "^{commit}").exit() == 0;
        } catch (IOException e) {
            return false;
        }
    }

    /** Every path, as installed, to its known hashes. */
    static SortedMap<String, SortedSet<String>> generate(Path repo) throws IOException {
        SortedMap<String, SortedSet<String>> files = new TreeMap<>();

        for (String commit : revList(repo, LIZ_ZIP)) {
            byte[] zip = blobAt(repo, commit, LIZ_ZIP);
            if (zip == null) {
                continue;
            }
            for (Map.Entry<String, byte[]> entry : zipFiles(zip).entrySet()) {
                String name = entry.getKey();
                if (name.startsWith(PREFIX)) {
                    add(files, name.substring(PREFIX.length()), entry.getValue());
                }
                if (commit.equals(FIRST_LAUNCHER_ZIP)) {
                    add(files, name, entry.getValue());
                }
            }
        }

        for (String commit : revList(repo, LIB_ZIP)) {
            byte[] zip = blobAt(repo, commit, LIB_ZIP);
            if (zip == null) {
                continue;
            }
            for (Map.Entry<String, byte[]> entry : zipFiles(zip).entrySet()) {
                // Its launchers stripped a "lib/" prefix too; this zip had none.
                String name = entry.getKey();
                add(files, name.startsWith(PREFIX) ? name.substring(PREFIX.length()) : name, entry.getValue());
            }
        }

        // path -> blob ids, then each distinct blob read once.
        Map<String, Set<String>> blobsByPath = new TreeMap<>();
        Set<String> blobs = new LinkedHashSet<>();
        String folder = ENGINE_LIBRARY + "/";
        for (String commit : revList(repo, ENGINE_LIBRARY)) {
            String listing = new String(run(repo, null, "ls-tree", "-r", "-z", commit, "--", ENGINE_LIBRARY)
                    .requireSuccess("ls-tree " + commit), StandardCharsets.UTF_8);
            for (String line : listing.split("\0")) {
                if (line.isEmpty()) {
                    continue;
                }
                // "<mode> <type> <id>\t<path>"
                int tab = line.indexOf('\t');
                String[] meta = line.substring(0, tab).split(" ");
                String path = line.substring(tab + 1);
                if (!meta[1].equals("blob") || !path.startsWith(folder)) {
                    continue;
                }
                blobsByPath.computeIfAbsent(path.substring(folder.length()), key -> new TreeSet<>()).add(meta[2]);
                blobs.add(meta[2]);
            }
        }
        Map<String, byte[]> contents = catFiles(repo, blobs);
        blobsByPath.forEach((path, ids) -> ids.forEach(id -> add(files, path, contents.get(id))));
        return files;
    }

    /** The resource's content: sorted, UTF-8, LF line endings. */
    static byte[] toJson(SortedMap<String, SortedSet<String>> files) {
        JsonObject root = new JsonObject();
        root.addProperty("formatVersion", LegacyLibraryHashes.FORMAT_VERSION);
        root.addProperty("hashAlgorithm", LibraryManifest.HASH_ALGORITHM);
        root.addProperty("lineEndings", LibraryManifest.LINE_ENDINGS);
        root.addProperty("through", THROUGH);
        JsonObject byPath = new JsonObject();
        files.forEach((path, hashes) -> {
            JsonArray array = new JsonArray();
            hashes.forEach(array::add);
            byPath.add(path, array);
        });
        root.add("files", byPath);
        String json = new GsonBuilder().setPrettyPrinting().disableHtmlEscaping().create().toJson(root) + "\n";
        return json.getBytes(StandardCharsets.UTF_8);
    }

    private static void add(SortedMap<String, SortedSet<String>> files, String path, byte[] content) {
        if (content == null || !LibraryManifest.isValidPath(path) || !LibraryManifest.isLibraryFile(path)) {
            return;
        }
        files.computeIfAbsent(path, key -> new TreeSet<>()).add(LibraryManifest.hash(content));
    }

    /** Every commit up to {@link #THROUGH} that touched {@code path}, without history simplification. */
    private static List<String> revList(Path repo, String path) throws IOException {
        String out = new String(run(repo, null, "rev-list", "--full-history", THROUGH, "--", path)
                .requireSuccess("rev-list " + path), StandardCharsets.UTF_8);
        List<String> commits = new ArrayList<>();
        for (String line : out.split("\n")) {
            if (!line.isBlank()) {
                commits.add(line.trim());
            }
        }
        return commits;
    }

    /** The file {@code path} at {@code commit}, or {@code null} when the commit has none (it deleted it). */
    private static byte[] blobAt(Path repo, String commit, String path) throws IOException {
        Result exists = run(repo, null, "cat-file", "-e", commit + ":" + path);
        if (exists.exit() != 0) {
            return null;
        }
        return run(repo, null, "cat-file", "blob", commit + ":" + path).requireSuccess("cat-file " + commit);
    }

    /** Every file entry of a zip, by name. */
    private static Map<String, byte[]> zipFiles(byte[] zip) throws IOException {
        Map<String, byte[]> files = new TreeMap<>();
        try (ZipInputStream in = new ZipInputStream(new ByteArrayInputStream(zip), StandardCharsets.UTF_8)) {
            for (ZipEntry entry = in.getNextEntry(); entry != null; entry = in.getNextEntry()) {
                if (!entry.isDirectory()) {
                    files.put(entry.getName(), in.readAllBytes());
                }
            }
        }
        return files;
    }

    /** Reads many blobs through one {@code git cat-file --batch}, its requests given as a file (no pipe deadlock). */
    private static Map<String, byte[]> catFiles(Path repo, Set<String> ids) throws IOException {
        Path requests = Files.createTempFile("vf-legacy-hashes-", ".txt");
        try {
            Files.writeString(requests, String.join("\n", ids) + "\n", StandardCharsets.US_ASCII);
            byte[] out = run(repo, requests, "cat-file", "--batch").requireSuccess("cat-file --batch");
            Map<String, byte[]> contents = new TreeMap<>();
            int at = 0;
            while (at < out.length) {
                int lineEnd = indexOf(out, (byte) '\n', at);
                String[] header = new String(out, at, lineEnd - at, StandardCharsets.US_ASCII).split(" ");
                if (header.length != 3 || !header[1].equals("blob")) {
                    throw new IOException("unexpected cat-file output: " + String.join(" ", header));
                }
                int size = Integer.parseInt(header[2]);
                int start = lineEnd + 1;
                byte[] content = new byte[size];
                System.arraycopy(out, start, content, 0, size);
                contents.put(header[0], content);
                at = start + size + 1;
            }
            return contents;
        } finally {
            Files.deleteIfExists(requests);
        }
    }

    private static int indexOf(byte[] bytes, byte value, int from) throws IOException {
        for (int i = from; i < bytes.length; i++) {
            if (bytes[i] == value) {
                return i;
            }
        }
        throw new IOException("truncated cat-file output");
    }

    /** A finished git command. */
    record Result(int exit, byte[] out) {
        byte[] requireSuccess(String what) throws IOException {
            if (exit != 0) {
                throw new IOException("git " + what + " failed with exit code " + exit);
            }
            return out;
        }
    }

    private static Result run(Path repo, Path stdin, String... args) throws IOException {
        List<String> command = new ArrayList<>();
        command.add("git");
        command.add("-C");
        command.add(repo.toString());
        // Bytes as stored: no line-ending or encoding conversion on the way out.
        command.add("-c");
        command.add("core.autocrlf=false");
        command.addAll(List.of(args));
        ProcessBuilder builder = new ProcessBuilder(command).redirectError(ProcessBuilder.Redirect.DISCARD);
        if (stdin != null) {
            builder.redirectInput(stdin.toFile());
        }
        Process process;
        try {
            process = builder.start();
        } catch (IOException e) {
            throw new IOException("git is not available: " + e.getMessage(), e);
        }
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        try (InputStream in = process.getInputStream()) {
            in.transferTo(out);
        }
        try {
            return new Result(process.waitFor(), out.toByteArray());
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("interrupted while running git", e);
        }
    }
}
