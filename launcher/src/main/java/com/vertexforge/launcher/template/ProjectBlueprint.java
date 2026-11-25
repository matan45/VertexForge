package com.vertexforge.launcher.template;

import com.vertexforge.launcher.engine.EngineContent;
import com.vertexforge.launcher.project.ProjectFiles;

import java.nio.file.Path;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Objects;
import java.util.Set;

/**
 * Every file a new project starts with, in the order they are written (VK-1689): the template's {@code project/}
 * files, the engine's script library, then each chosen option's folder. Besides these the creator writes the
 * {@code .vfproj} first and the library's {@link LibraryManifest} last.
 * <p>
 * The path budget needs the deepest file and the progress bar the count, so both come from the real file list.
 */
public final class ProjectBlueprint {

    /**
     * One file to copy.
     *
     * @param source      the engine file
     * @param target      where it goes, '/'-separated and relative to the project folder
     * @param fill        how its placeholders are filled in
     * @param libraryPath for a script library file, its path relative to the library ('/'-separated), the key of its
     *                    hash in {@link LibraryManifest}; {@code null} for any other file
     */
    public record Entry(Path source, String target, Placeholders.Fill fill, String libraryPath) {
        public Entry {
            Objects.requireNonNull(source, "source");
            Objects.requireNonNull(target, "target");
            Objects.requireNonNull(fill, "fill");
        }
    }

    private final EngineContent engine;
    private final TemplateManifest template;
    private final List<Entry> entries;
    private final int deepestTemplateFile;

    private ProjectBlueprint(EngineContent engine, TemplateManifest template, List<Entry> entries) {
        this.engine = engine;
        this.template = template;
        this.entries = List.copyOf(entries);
        int deepest = Math.max(libraryManifestTarget().length(), template.startupScenePath().length());
        for (Entry entry : this.entries) {
            deepest = Math.max(deepest, entry.target().length());
        }
        this.deepestTemplateFile = deepest;
    }

    /**
     * A blueprint for these files.
     *
     * @throws TemplateException when two files would get the same path (Windows ignores case), or a file would take a
     *                           folder's place
     */
    public static ProjectBlueprint of(EngineContent engine, TemplateManifest template, List<Entry> entries)
            throws TemplateException {
        Objects.requireNonNull(engine, "engine");
        Objects.requireNonNull(template, "template");
        ProjectBlueprint blueprint = new ProjectBlueprint(engine, template, entries);

        Set<String> files = new HashSet<>();
        files.add(key(blueprint.libraryManifestTarget()));
        for (Entry entry : blueprint.entries) {
            if (!files.add(key(entry.target()))) {
                throw new TemplateException("Two files of the template \"" + template.id() + "\" would both be "
                        + entry.target() + ".");
            }
        }
        for (String file : files) {
            for (int slash = file.indexOf('/'); slash >= 0; slash = file.indexOf('/', slash + 1)) {
                if (files.contains(file.substring(0, slash))) {
                    throw new TemplateException("The template \"" + template.id() + "\" would write both a file "
                            + file.substring(0, slash) + " and a folder of that name.");
                }
            }
        }
        return blueprint;
    }

    public EngineContent engine() {
        return engine;
    }

    public TemplateManifest template() {
        return template;
    }

    /** The files to copy, in order. */
    public List<Entry> entries() {
        return entries;
    }

    /** {@code scripts/lib/.vflib.json} under the template's library folder. */
    public String libraryManifestTarget() {
        return template.scriptLibrary() + "/" + LibraryManifest.FILE_NAME;
    }

    /** Every file a creation writes: the {@code .vfproj}, the entries and the {@link LibraryManifest}. */
    public int fileCount() {
        return 1 + entries.size() + 1;
    }

    /**
     * The length, in characters, of the deepest file a project called {@code name} starts with, relative to its
     * project folder: the deepest file written (the startup scene the editor builds counts too), or
     * {@code <name>.vfproj} when that is longer.
     */
    public int deepestRelativeLength(String name) {
        return Math.max(deepestTemplateFile, name.length() + ProjectFiles.EXTENSION.length());
    }

    private static String key(String target) {
        return target.toLowerCase(Locale.ROOT);
    }
}
