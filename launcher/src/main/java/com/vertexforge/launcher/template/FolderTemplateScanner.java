package com.vertexforge.launcher.template;

import com.vertexforge.launcher.engine.EngineContent;
import com.vertexforge.launcher.project.ProjectFiles;
import com.vertexforge.launcher.util.FileErrors;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.EnumMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Optional;
import java.util.stream.Stream;

/**
 * {@link TemplateScanner#SYSTEM}: reads an engine's script library, templates and option folders from disk (VK-1689).
 * <p>
 * Only regular files are taken. Links are never followed inside a folder: a symbolic link, or a junction that leads
 * elsewhere, is left out with a note. A folder or file whose real path is its own (a OneDrive placeholder is a reparse
 * point too) is read as usual.
 */
final class FolderTemplateScanner {

    private FolderTemplateScanner() {
        // Utility class - prevent instantiation
    }

    static TemplateCatalog scan(EngineContent engine) throws IOException {
        List<String> notes = new ArrayList<>();

        List<TemplateCatalog.FileEntry> library = new ArrayList<>(
                listFolder(engine.scriptLibDir(), "The script library", "script library", notes));
        if (library.removeIf(file -> file.relative().equalsIgnoreCase(LibraryManifest.FILE_NAME))) {
            notes.add("The script library's own " + LibraryManifest.FILE_NAME + " was left out.");
        }
        // The editor's sidecars belong to the project they were made in, and .vflib.json never lists them (VK-1691).
        if (library.removeIf(file -> LibraryManifest.isSidecar(file.relative()))) {
            notes.add("The script library's editor files (" + LibraryManifest.SIDECAR_EXTENSION + ") were left out.");
        }
        if (library.isEmpty()) {
            throw new IOException("The script library " + engine.scriptLibDir() + " has no files.");
        }

        requireFolder(engine.templatesDir(), "The templates folder");
        List<TemplateCatalog.Template> templates = new ArrayList<>();
        for (Path folder : subfolders(engine.templatesDir(), notes)) {
            String name = folder.getFileName().toString();
            try {
                templates.add(readTemplate(engine, folder, notes));
            } catch (TemplateException e) {
                notes.add("The template \"" + name + "\" was skipped: " + e.getMessage());
            } catch (IOException e) {
                notes.add("The template \"" + name + "\" could not be read: " + FileErrors.describe(e));
            }
        }
        templates.sort(Comparator.comparing((TemplateCatalog.Template t) ->
                !t.id().equals(TemplateCatalog.DEFAULT_TEMPLATE)).thenComparing(TemplateCatalog.Template::id));

        Map<TemplateOptionKind, List<TemplateCatalog.FileEntry>> optionFiles = new EnumMap<>(TemplateOptionKind.class);
        Map<TemplateOptionKind, String> unavailable = new EnumMap<>(TemplateOptionKind.class);
        for (TemplateOptionKind kind : TemplateOptionKind.values()) {
            Optional<Path> source = kind.sourceIn(engine);
            if (source.isEmpty()) {
                unavailable.put(kind, "this engine's engine.json names no " + kind.manifestField() + " folder.");
                continue;
            }
            try {
                List<TemplateCatalog.FileEntry> files = listFolder(source.get(), "The folder", "folder", notes);
                if (files.isEmpty()) {
                    unavailable.put(kind, source.get() + " has no files.");
                } else {
                    optionFiles.put(kind, files);
                }
            } catch (IOException e) {
                unavailable.put(kind, FileErrors.describe(e));
            }
        }
        return new TemplateCatalog(engine, library, templates, optionFiles, unavailable, notes);
    }

    private static TemplateCatalog.Template readTemplate(EngineContent engine, Path folder, List<String> notes)
            throws IOException, TemplateException {
        Path manifestFile = folder.resolve(TemplateManifest.FILE_NAME);
        if (!Files.isRegularFile(manifestFile)) {
            throw new TemplateException("it has no " + TemplateManifest.FILE_NAME + ".");
        }
        TemplateManifest manifest = TemplateManifest.read(manifestFile);
        String folderName = folder.getFileName().toString();
        if (!manifest.id().equals(folderName)) {
            throw new TemplateException("its id \"" + manifest.id() + "\" is not its folder's name.");
        }
        if (!manifest.supportsEngine(engine.engineVersion())) {
            throw new TemplateException("it needs " + EngineContent.PRODUCT_NAME + " "
                    + manifest.minimumEngineVersion() + " or newer.");
        }

        Path payload = folder.resolve(TemplateManifest.PAYLOAD_DIR);
        if (!Files.isDirectory(payload)) {
            throw new TemplateException("it has no " + TemplateManifest.PAYLOAD_DIR + " folder.");
        }
        List<TemplateCatalog.FileEntry> files = listFolder(payload, "Its " + TemplateManifest.PAYLOAD_DIR + " folder",
                "template \"" + folderName + "\"", notes);
        for (TemplateCatalog.FileEntry file : files) {
            String relative = file.relative();
            if (relative.indexOf('/') < 0
                    && relative.toLowerCase(Locale.ROOT).endsWith(ProjectFiles.EXTENSION.toLowerCase(Locale.ROOT))) {
                throw new TemplateException("its " + TemplateManifest.PAYLOAD_DIR + " folder holds " + relative
                        + ": the launcher writes the project file itself.");
            }
            if (TemplateManifest.isEditorState(relative)) {
                throw new TemplateException(relative + " lies in the editor's own "
                        + TemplateManifest.EDITOR_STATE_DIR + " folder.");
            }
            if (Placeholders.fillFor(relative) != Placeholders.Fill.NONE
                    && !Placeholders.isUtf8(Files.readAllBytes(file.source()))) {
                throw new TemplateException(relative + " is not valid UTF-8 text.");
            }
        }
        return new TemplateCatalog.Template(manifest, folder, files);
    }

    private static void requireFolder(Path folder, String what) throws IOException {
        BasicFileAttributes attributes;
        try {
            attributes = Files.readAttributes(folder, BasicFileAttributes.class);
        } catch (NoSuchFileException e) {
            throw new IOException(what + " " + folder + " does not exist.");
        }
        if (!attributes.isDirectory()) {
            throw new IOException(what + " " + folder + " is not a folder.");
        }
    }

    /** The folders directly in {@code folder}, by name; links are left out with a note. */
    private static List<Path> subfolders(Path folder, List<String> notes) throws IOException {
        List<Path> folders = new ArrayList<>();
        for (Path child : children(folder)) {
            BasicFileAttributes attributes = Files.readAttributes(child, BasicFileAttributes.class,
                    LinkOption.NOFOLLOW_LINKS);
            if (attributes.isDirectory() && (!attributes.isOther() || isOwnFolder(child))) {
                folders.add(child);
            } else if (attributes.isSymbolicLink() || attributes.isDirectory()) {
                notes.add("The template folder " + child.getFileName() + " is a link and was skipped.");
            }
        }
        return folders;
    }

    /**
     * Every regular file in {@code root} and its subfolders, sorted by relative path.
     *
     * @param what  how a message names the folder ("The script library")
     * @param owner how a note names what the files belong to ("script library")
     */
    static List<TemplateCatalog.FileEntry> listFolder(Path root, String what, String owner, List<String> notes)
            throws IOException {
        requireFolder(root, what);
        List<TemplateCatalog.FileEntry> files = new ArrayList<>();
        walk(root, "", files, owner, notes);
        files.sort(Comparator.comparing(TemplateCatalog.FileEntry::relative));
        return files;
    }

    private static void walk(Path folder, String prefix, List<TemplateCatalog.FileEntry> files, String owner,
                             List<String> notes) throws IOException {
        for (Path child : children(folder)) {
            String name = child.getFileName().toString();
            String relative = prefix.isEmpty() ? name : prefix + "/" + name;
            BasicFileAttributes attributes = Files.readAttributes(child, BasicFileAttributes.class,
                    LinkOption.NOFOLLOW_LINKS);
            if (attributes.isSymbolicLink()) {
                notes.add("The " + owner + "'s " + relative + " is a link and was left out.");
            } else if (attributes.isDirectory()) {
                // A junction reads as a folder that is also "other": entered only when it does not lead elsewhere.
                if (!attributes.isOther() || isOwnFolder(child)) {
                    walk(child, relative, files, owner, notes);
                } else {
                    notes.add("The " + owner + "'s " + relative + " is a link to another folder and was left out.");
                }
            } else if (attributes.isRegularFile() || (attributes.isOther() && Files.isRegularFile(child))) {
                files.add(new TemplateCatalog.FileEntry(child, relative));
            } else {
                notes.add("The " + owner + "'s " + relative + " is not a regular file and was left out.");
            }
        }
    }

    private static List<Path> children(Path folder) throws IOException {
        try (Stream<Path> listing = Files.list(folder)) {
            return listing.sorted(Comparator.comparing(child -> child.getFileName().toString())).toList();
        }
    }

    /** Whether {@code folder} really lives where it is listed, rather than redirecting elsewhere (a junction). */
    private static boolean isOwnFolder(Path folder) {
        Path parent = folder.getParent();
        Path name = folder.getFileName();
        if (parent == null || name == null) {
            return false;
        }
        try {
            return folder.toRealPath().equals(parent.toRealPath().resolve(name));
        } catch (IOException e) {
            return false;
        }
    }
}
