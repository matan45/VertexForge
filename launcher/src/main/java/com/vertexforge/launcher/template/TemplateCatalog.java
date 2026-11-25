package com.vertexforge.launcher.template;

import com.vertexforge.launcher.engine.EngineContent;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.EnumMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;

/**
 * What one engine offers for new projects, as a {@link TemplateScanner} just read it (VK-1689): its script library,
 * its usable templates, and the folders its options copy. Pure once built: {@link #blueprint} runs on any thread.
 *
 * @param engine             the engine that was read
 * @param library            the script library's files
 * @param templates          the usable templates, {@code empty} first, then by folder name
 * @param optionFiles        the files of each option whose engine folder could be read
 * @param unavailableOptions why each other option cannot be offered
 * @param notes              templates that were skipped and files that were left out, each with the reason
 */
public record TemplateCatalog(
        EngineContent engine,
        List<FileEntry> library,
        List<Template> templates,
        Map<TemplateOptionKind, List<FileEntry>> optionFiles,
        Map<TemplateOptionKind, String> unavailableOptions,
        List<String> notes
) {
    /** The id of the template the dialog selects first when the engine has it. */
    public static final String DEFAULT_TEMPLATE = "empty";

    /**
     * One file found in an engine folder.
     *
     * @param source   the file
     * @param relative its path in the folder that was read, '/'-separated
     */
    public record FileEntry(Path source, String relative) {
        public FileEntry {
            Objects.requireNonNull(source, "source");
            Objects.requireNonNull(relative, "relative");
        }
    }

    /**
     * A usable template.
     *
     * @param manifest its {@code template.json}
     * @param folder   its folder
     * @param payload  the files of its {@code project/} folder
     */
    public record Template(TemplateManifest manifest, Path folder, List<FileEntry> payload) {
        public Template {
            Objects.requireNonNull(manifest, "manifest");
            Objects.requireNonNull(folder, "folder");
            payload = List.copyOf(payload);
        }

        public String id() {
            return manifest.id();
        }
    }

    public TemplateCatalog {
        Objects.requireNonNull(engine, "engine");
        library = List.copyOf(library);
        templates = List.copyOf(templates);
        EnumMap<TemplateOptionKind, List<FileEntry>> files = new EnumMap<>(TemplateOptionKind.class);
        optionFiles.forEach((kind, list) -> files.put(kind, List.copyOf(list)));
        optionFiles = java.util.Collections.unmodifiableMap(files);
        EnumMap<TemplateOptionKind, String> reasons = new EnumMap<>(TemplateOptionKind.class);
        reasons.putAll(unavailableOptions);
        unavailableOptions = java.util.Collections.unmodifiableMap(reasons);
        notes = List.copyOf(notes);
    }

    public Optional<Template> find(String id) {
        return templates.stream().filter(template -> template.id().equals(id)).findFirst();
    }

    /** Why {@code kind} cannot be offered; empty when its folder was read. */
    public Optional<String> unavailableReason(TemplateOptionKind kind) {
        if (optionFiles.containsKey(kind)) {
            return Optional.empty();
        }
        return Optional.of(unavailableOptions.getOrDefault(kind, "This engine does not have it."));
    }

    /**
     * The files a project made from template {@code templateId} with the options {@code optionIds} starts with.
     *
     * @throws TemplateException when the engine has no such template, the template offers no such option, or an
     *                           option's folder cannot be read
     */
    public ProjectBlueprint blueprint(String templateId, Set<String> optionIds) throws TemplateException {
        Objects.requireNonNull(optionIds, "optionIds");
        Template template = find(templateId).orElseThrow(() -> new TemplateException(
                "The engine has no usable template \"" + templateId + "\"."));
        TemplateManifest manifest = template.manifest();

        List<ProjectBlueprint.Entry> entries = new ArrayList<>();
        for (FileEntry file : template.payload()) {
            entries.add(new ProjectBlueprint.Entry(file.source(), file.relative(),
                    Placeholders.fillFor(file.relative()), null));
        }
        for (FileEntry file : library) {
            entries.add(new ProjectBlueprint.Entry(file.source(), manifest.scriptLibrary() + "/" + file.relative(),
                    Placeholders.Fill.NONE, file.relative()));
        }

        Set<String> offered = new HashSet<>();
        for (TemplateManifest.Option option : manifest.options()) {
            offered.add(option.id());
            if (!optionIds.contains(option.id())) {
                continue;
            }
            TemplateOptionKind kind = TemplateOptionKind.forId(option.id()).orElseThrow(() -> new TemplateException(
                    "This launcher cannot add \"" + option.name() + "\" (" + option.id() + ")."));
            List<FileEntry> files = optionFiles.get(kind);
            if (files == null) {
                throw new TemplateException("\"" + option.name() + "\" cannot be added: "
                        + unavailableReason(kind).orElse(""));
            }
            for (FileEntry file : files) {
                entries.add(new ProjectBlueprint.Entry(file.source(), option.target() + "/" + file.relative(),
                        Placeholders.Fill.NONE, null));
            }
        }
        for (String id : optionIds) {
            if (!offered.contains(id)) {
                throw new TemplateException("The template \"" + templateId + "\" has no option \"" + id + "\".");
            }
        }
        return ProjectBlueprint.of(engine, manifest, entries);
    }
}
