package com.vertexforge.launcher.service;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.FileTime;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.time.Instant;
import java.util.HexFormat;
import java.util.Map;
import java.util.Random;
import java.util.TreeMap;
import java.util.function.Predicate;
import java.util.stream.Stream;

/** A project as the editor leaves it, for the backup and copy tests (VK-1690). */
final class ProjectTreeFixture {

    /** The fixture's {@code .vfproj}, with CRLF line ends as a Windows editor might leave them. */
    static final String PROJECT_JSON = "{\r\n"
            + "  \"schemaVersion\": \"1.0\",\r\n"
            + "  \"projectName\": \"Game\",\r\n"
            + "  \"version\": \"0.1.0\",\r\n"
            + "  \"workingDirectory\": \"assets\",\r\n"
            + "  \"startupScene\": \"scenes/Main.vfScene\",\r\n"
            + "  \"exeIconPath\": \"\",\r\n"
            + "  \"engineVersion\": \"VertexForge 1.0.0\",\r\n"
            + "  \"lastModified\": \"2026-01-02T03:04:05Z\"\r\n"
            + "}\r\n";
    /** The size of the fixture's big file: more than one 64 KiB chunk. */
    static final int BIG_FILE_BYTES = 100 * 1024 + 17;

    private ProjectTreeFixture() {
    }

    /**
     * {@code parent/Game}, working directory {@code assets}: the {@code .vfproj}, a {@code .gitignore}, a script, a
     * big texture, a {@code .vfmeta}, an empty {@code scenes} folder, and what a backup or a copy leaves out: the
     * editor's state folder (with its lock) and the compiled scripts. Every file has a whole-second modification
     * time of its own.
     */
    static Path game(Path parent) {
        try {
            Path game = Files.createDirectories(parent.resolve("Game"));
            write(game.resolve("Game.vfproj"), PROJECT_JSON.getBytes(StandardCharsets.UTF_8), 1);
            write(game.resolve(".gitignore"), "/.vertexforge/\r\n/assets/scripts/compiled/\r\n"
                    .getBytes(StandardCharsets.UTF_8), 2);
            write(game.resolve("assets/scripts/game/Main.mt"), "@Script\npublic class Main {\n}\n"
                    .getBytes(StandardCharsets.UTF_8), 3);
            write(game.resolve("assets/scripts/game/Main.mt.vfmeta"), "{\"guid\": \"1690\"}\n"
                    .getBytes(StandardCharsets.UTF_8), 4);
            byte[] big = new byte[BIG_FILE_BYTES];
            new Random(1690).nextBytes(big);
            write(game.resolve("assets/textures/big.bin"), big, 5);
            Files.createDirectories(game.resolve("assets/scenes"));
            write(game.resolve("assets/scripts/compiled/scripts.mtcLib"), "compiled".getBytes(StandardCharsets.UTF_8),
                    6);
            write(game.resolve(".vertexforge/editor.lock"), "{\"pid\":42}".getBytes(StandardCharsets.UTF_8), 7);
            write(game.resolve(".vertexforge/saves/slot1.json"), "{}".getBytes(StandardCharsets.UTF_8), 8);
            return game;
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    private static void write(Path file, byte[] content, int day) throws IOException {
        Files.createDirectories(file.getParent());
        Files.write(file, content);
        Files.setLastModifiedTime(file, FileTime.from(Instant.parse("2025-03-0" + day + "T10:20:30Z")));
    }

    /**
     * Every folder ({@code "folder"}) and file (its size and SHA-256) below {@code root}, by '/'-relative path, except
     * the paths {@code skipped} says: what must survive a backup or a copy byte for byte.
     */
    static Map<String, String> contentHashes(Path root, Predicate<String> skipped) {
        try (Stream<Path> paths = Files.walk(root)) {
            Map<String, String> hashes = new TreeMap<>();
            for (Path path : paths.toList()) {
                if (path.equals(root)) {
                    continue;
                }
                String relative = root.relativize(path).toString().replace('\\', '/');
                if (skipped.test(relative)) {
                    continue;
                }
                if (Files.isDirectory(path)) {
                    hashes.put(relative, "folder");
                } else {
                    byte[] bytes = Files.readAllBytes(path);
                    hashes.put(relative, bytes.length + " " + HexFormat.of().formatHex(
                            MessageDigest.getInstance("SHA-256").digest(bytes)));
                }
            }
            return hashes;
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);
        }
    }
}
