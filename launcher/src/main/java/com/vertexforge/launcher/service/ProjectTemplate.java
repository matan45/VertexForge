package com.vertexforge.launcher.service;

import com.vertexforge.launcher.util.ProjectValidator;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * The files a new project starts with, as far as their number and their paths are concerned (VK-1679): the path
 * budget needs the deepest one, the progress bar needs the count. The bundled script library is scanned once, so
 * both follow any change to {@code assets/scripts/lib} (and, later, templates that come from the engine, VK-1689).
 */
public final class ProjectTemplate {

    /** Where the library is extracted, relative to the project folder. */
    static final String LIBRARY_DIR = "scripts/lib/";
    /** The template files besides the project file and the library, relative to the project folder. */
    static final List<String> FIXED_FILES = List.of(
            "scenes/Main.vfScene",
            "scripts/scripts.mtproj",
            "scripts/game/Main.mt");

    /** Loads a template; production uses {@link #BUNDLED}. */
    @FunctionalInterface
    public interface Source {
        ProjectTemplate load() throws IOException;
    }

    public static final Source BUNDLED = ProjectTemplate::bundled;

    private static final Object BUNDLED_LOCK = new Object();
    private static volatile ProjectTemplate bundled;

    private final List<String> libraryFiles;
    private final int deepestTemplateFile;

    private ProjectTemplate(List<String> libraryFiles) {
        this.libraryFiles = List.copyOf(libraryFiles);
        int deepest = 0;
        for (String file : FIXED_FILES) {
            deepest = Math.max(deepest, file.length());
        }
        for (String file : this.libraryFiles) {
            deepest = Math.max(deepest, (LIBRARY_DIR + file).length());
        }
        this.deepestTemplateFile = deepest;
    }

    /** A template whose library holds these files ('/'-separated, relative to {@code scripts/lib}). */
    public static ProjectTemplate of(List<String> libraryFiles) {
        return new ProjectTemplate(libraryFiles);
    }

    /**
     * The template the launcher ships: its library is read from {@link ProjectCreator#LIBRARY_ZIP_RESOURCE} on first
     * use and kept. A missing library is reported every time (it is a broken build), never cached.
     */
    public static ProjectTemplate bundled() throws IOException {
        ProjectTemplate template = bundled;
        if (template != null) {
            return template;
        }
        synchronized (BUNDLED_LOCK) {
            if (bundled == null) {
                bundled = new ProjectTemplate(scanLibrary());
            }
            return bundled;
        }
    }

    /** The library's files, '/'-separated and relative to {@code scripts/lib}. */
    public List<String> libraryFiles() {
        return libraryFiles;
    }

    /** Every file a creation writes: the project file, the fixed files and the library. */
    public int fileCount() {
        return 1 + FIXED_FILES.size() + libraryFiles.size();
    }

    /**
     * The length, in characters, of the deepest file a project called {@code name} starts with, relative to its
     * project folder: the deepest template file, or {@code <name>.vfproj} when the name is longer than that.
     */
    public int deepestRelativeLength(String name) {
        return Math.max(deepestTemplateFile, name.length() + ProjectValidator.PROJECT_EXTENSION.length());
    }

    static IOException missingLibrary() {
        return new IOException("Script library " + ProjectCreator.LIBRARY_ZIP_RESOURCE + " is missing from the "
                + "launcher. It is packed from assets/scripts/lib at build time: rebuild the launcher with "
                + "'mvnw package' in launcher/ (see launcher/build.txt).");
    }

    private static List<String> scanLibrary() throws IOException {
        try (InputStream resource = ProjectTemplate.class.getResourceAsStream(ProjectCreator.LIBRARY_ZIP_RESOURCE)) {
            if (resource == null) {
                throw missingLibrary();
            }
            List<String> files = new ArrayList<>();
            try (ZipInputStream zip = new ZipInputStream(resource)) {
                ZipEntry entry;
                while ((entry = zip.getNextEntry()) != null) {
                    if (!entry.isDirectory()) {
                        files.add(entry.getName());
                    }
                }
            }
            return files;
        }
    }
}
