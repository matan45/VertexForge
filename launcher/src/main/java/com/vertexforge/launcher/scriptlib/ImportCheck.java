package com.vertexforge.launcher.scriptlib;

import com.vertexforge.launcher.template.LibraryManifest;

import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.function.Predicate;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Finds the imports a script-library update would break (VK-1691): imports in the user's scripts that find their file
 * in the library as it is, but not in the library the update builds. The preview warns about them; they never stop
 * the update.
 * <p>
 * Imports are found and resolved as {@code VFEngine/tests/test_repo_scan_helpers.hpp} does, which mirrors mType's
 * resolver ({@code ImportManager_Resolution.cpp}): comments are left out (a string keeps a {@code //}), every
 * {@code import ... from "<target>"} is taken, and a target is an absolute path as it is, else it is looked for next to
 * the importing file, then in the scripts folder (the {@code .mtproj}'s), then in each search path of the
 * {@code .mtproj} below it.
 */
public final class ImportCheck {

    private static final Pattern IMPORT = Pattern.compile("import\\s+[^;\"]*?from\\s+\"([^\"]+)\"");

    /**
     * A script to check.
     *
     * @param file    the script itself
     * @param display its path as the user knows it, relative to the scripts folder ({@code game/Main.mt})
     * @param text    its content
     */
    public record Script(Path file, String display, String text) {
        public Script {
            Objects.requireNonNull(file, "file");
            Objects.requireNonNull(display, "display");
            Objects.requireNonNull(text, "text");
        }
    }

    /**
     * An import the update would break.
     *
     * @param script     the importing script, as {@link Script#display}
     * @param target     the import's path as written
     * @param suggestion the library-relative path of the only file of the new library with the same name, when there
     *                   is exactly one; {@code null} otherwise
     */
    public record BrokenImport(String script, String target, String suggestion) {
        public BrokenImport {
            Objects.requireNonNull(script, "script");
            Objects.requireNonNull(target, "target");
        }
    }

    /**
     * The libraries before and after the update.
     *
     * @param scriptsFolder the folder of the {@code .mtproj}, absolute and normalized
     * @param libraryFolder the library folder in it, absolute and normalized
     * @param searchPaths   the {@code .mtproj}'s search paths, relative to the scripts folder
     * @param before        the keys of every file in the library now
     * @param after         the exact paths of every file in the new library
     * @param existsElsewhere whether a file outside the library exists (the file system, in production)
     */
    public record Libraries(Path scriptsFolder, Path libraryFolder, List<String> searchPaths, Set<String> before,
                            Collection<String> after, Predicate<Path> existsElsewhere) {
        public Libraries {
            Objects.requireNonNull(scriptsFolder, "scriptsFolder");
            Objects.requireNonNull(libraryFolder, "libraryFolder");
            searchPaths = List.copyOf(searchPaths);
            before = Set.copyOf(before);
            after = List.copyOf(after);
            Objects.requireNonNull(existsElsewhere, "existsElsewhere");
        }
    }

    private ImportCheck() {
        // Utility class - prevent instantiation
    }

    /** Every import of {@code scripts} that resolves now but not after the update, in the order found. */
    public static List<BrokenImport> check(Collection<Script> scripts, Libraries libraries) {
        Objects.requireNonNull(scripts, "scripts");
        Objects.requireNonNull(libraries, "libraries");
        Set<String> afterKeys = new HashSet<>();
        libraries.after().forEach(path -> afterKeys.add(LibraryManifest.pathKey(path)));

        List<BrokenImport> broken = new ArrayList<>();
        for (Script script : scripts) {
            for (String target : imports(script.text())) {
                if (resolves(script.file(), target, libraries, libraries.before())
                        && !resolves(script.file(), target, libraries, afterKeys)) {
                    broken.add(new BrokenImport(script.display(), target, suggestion(target, libraries.after())));
                }
            }
        }
        return List.copyOf(broken);
    }

    /** The targets of every {@code import ... from "..."} outside comments, in order. */
    public static List<String> imports(String code) {
        Matcher matcher = IMPORT.matcher(stripComments(code));
        List<String> targets = new ArrayList<>();
        while (matcher.find()) {
            targets.add(matcher.group(1));
        }
        return targets;
    }

    /**
     * {@code code} without its {@code //} and {@code /* *}{@code /} comments; a string or character literal is kept
     * whole, so a comment marker inside one stays. Line breaks inside a comment are kept.
     */
    public static String stripComments(String code) {
        StringBuilder out = new StringBuilder(code.length());
        State state = State.CODE;
        for (int i = 0; i < code.length(); i++) {
            char c = code.charAt(i);
            char next = i + 1 < code.length() ? code.charAt(i + 1) : '\0';
            switch (state) {
                case CODE -> {
                    if (c == '/' && next == '/') {
                        state = State.LINE_COMMENT;
                        i++;
                    } else if (c == '/' && next == '*') {
                        state = State.BLOCK_COMMENT;
                        i++;
                    } else {
                        if (c == '"') {
                            state = State.STRING;
                        } else if (c == '\'') {
                            state = State.CHAR;
                        }
                        out.append(c);
                    }
                }
                case LINE_COMMENT -> {
                    if (c == '\n') {
                        state = State.CODE;
                        out.append(c);
                    }
                }
                case BLOCK_COMMENT -> {
                    if (c == '*' && next == '/') {
                        state = State.CODE;
                        i++;
                    } else if (c == '\n') {
                        out.append(c);
                    }
                }
                case STRING, CHAR -> {
                    out.append(c);
                    if (c == '\\' && next != '\0') {
                        out.append(next);
                        i++;
                    } else if (c == (state == State.STRING ? '"' : '\'')) {
                        state = State.CODE;
                    }
                }
            }
        }
        return out.toString();
    }

    private enum State {
        CODE,
        LINE_COMMENT,
        BLOCK_COMMENT,
        STRING,
        CHAR
    }

    private static boolean resolves(Path importingFile, String target, Libraries libraries, Set<String> library) {
        Path relative;
        try {
            relative = Path.of(target);
        } catch (InvalidPathException e) {
            return false;
        }
        if (relative.isAbsolute()) {
            return exists(relative.normalize(), libraries, library);
        }
        List<Path> candidates = new ArrayList<>();
        Path folder = importingFile.getParent();
        if (folder != null) {
            candidates.add(folder.resolve(relative));
        }
        candidates.add(libraries.scriptsFolder().resolve(relative));
        for (String searchPath : libraries.searchPaths()) {
            try {
                candidates.add(libraries.scriptsFolder().resolve(searchPath).resolve(relative));
            } catch (InvalidPathException e) {
                // A search path that is not a path finds nothing.
            }
        }
        for (Path candidate : candidates) {
            if (exists(candidate.normalize(), libraries, library)) {
                return true;
            }
        }
        return false;
    }

    private static boolean exists(Path path, Libraries libraries, Set<String> library) {
        Optional<String> inLibrary = relativeTo(libraries.libraryFolder(), path);
        if (inLibrary.isPresent()) {
            return library.contains(LibraryManifest.pathKey(inLibrary.get()));
        }
        return libraries.existsElsewhere().test(path);
    }

    /**
     * {@code path} relative to {@code folder}, '/'-separated, when it lies below it (compared name by name, ignoring
     * case, as Windows does); empty otherwise, and for the folder itself.
     */
    static Optional<String> relativeTo(Path folder, Path path) {
        if (path.getNameCount() <= folder.getNameCount()) {
            return Optional.empty();
        }
        Path folderRoot = folder.getRoot();
        Path pathRoot = path.getRoot();
        if (folderRoot == null || pathRoot == null
                || !LibraryManifest.pathKey(folderRoot.toString()).equals(LibraryManifest.pathKey(pathRoot.toString()))) {
            return Optional.empty();
        }
        for (int i = 0; i < folder.getNameCount(); i++) {
            if (!LibraryManifest.pathKey(folder.getName(i).toString())
                    .equals(LibraryManifest.pathKey(path.getName(i).toString()))) {
                return Optional.empty();
            }
        }
        StringBuilder relative = new StringBuilder();
        for (int i = folder.getNameCount(); i < path.getNameCount(); i++) {
            if (!relative.isEmpty()) {
                relative.append('/');
            }
            relative.append(path.getName(i));
        }
        return Optional.of(relative.toString());
    }

    private static String suggestion(String target, Collection<String> after) {
        String normalized = target.replace('\\', '/');
        String name = normalized.substring(normalized.lastIndexOf('/') + 1);
        if (name.isEmpty()) {
            return null;
        }
        String key = LibraryManifest.pathKey(name);
        String found = null;
        for (String path : after) {
            String fileName = path.substring(path.lastIndexOf('/') + 1);
            if (LibraryManifest.pathKey(fileName).equals(key)) {
                if (found != null) {
                    return null;
                }
                found = path;
            }
        }
        return found;
    }
}
