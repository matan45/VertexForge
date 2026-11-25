package com.vertexforge.launcher.scriptlib;

import com.vertexforge.launcher.scriptlib.FileDecision.Verdict;
import com.vertexforge.launcher.template.LibraryManifest;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HexFormat;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

/**
 * What a script-library update builds (VK-1691): the files and folders of the new library, where each comes from, and
 * the new {@code .vflib.json}. Pure: worked out from the two libraries' listings, their hashes and the baseline.
 * <ul>
 *   <li>Every file the {@linkplain LibraryClassifier classifier} keeps is copied from the project unchanged (its bytes,
 *       so its line endings stay); every file it adds, restores, updates or (in a conflict, unless the user keeps
 *       theirs) replaces is copied from the engine.</li>
 *   <li>A path the engine also has is spelled as the engine spells it, folders included; anything else keeps the
 *       project's spelling.</li>
 *   <li>An editor sidecar ({@code X.vfmeta}) follows its file: it is kept (with its GUID) while the file stays, under
 *       the file's new spelling, and dropped with a deleted file. A sidecar of no library file is kept.</li>
 *   <li>A folder is kept when it holds anything that is kept, when the engine has it, or when it held no file at all
 *       before the update (an empty folder of the user's). A folder only the update's deletions emptied is dropped.</li>
 *   <li>The new {@code .vflib.json} records the ENGINE's hash of every engine file, never the user's: a file the user
 *       keeps then reads as "changed by the user, not by the engine" on the next update, which keeps it again, until
 *       the engine changes it once more.</li>
 * </ul>
 * A plan is {@linkplain #upToDate() up to date} when building it would change nothing: then nothing is written at all.
 */
public final class LibraryPlan {

    /** Where a file of the new library is copied from. */
    public enum Source {
        PROJECT,
        ENGINE
    }

    /**
     * One file of the new library.
     *
     * @param path         its exact path in the new library
     * @param source       which library it is copied from
     * @param sourcePath   its exact path there
     * @param expectedHash its {@link LibraryManifest#hash}, checked while it is copied; {@code null} for a sidecar
     */
    public record TargetFile(String path, Source source, String sourcePath, String expectedHash) {
        public TargetFile {
            Objects.requireNonNull(path, "path");
            Objects.requireNonNull(source, "source");
            Objects.requireNonNull(sourcePath, "sourcePath");
        }
    }

    /**
     * Everything a plan is made from.
     *
     * @param project        the project's library files (empty when it has no library folder)
     * @param projectFiles   every file below the project's library folder, '/'-relative: library files, sidecars and
     *                       its {@code .vflib.json}
     * @param projectFolders every folder below the project's library folder, '/'-relative
     * @param engine         the engine's library files
     * @param engineFolders  every folder below the engine's library folder, '/'-relative
     * @param baseline       the project's baseline
     * @param engineVersion  the engine's {@code MAJOR.MINOR.PATCH}, recorded in the new {@code .vflib.json}
     */
    public record Inputs(LibrarySnapshot project, List<String> projectFiles, List<String> projectFolders,
                         LibrarySnapshot engine, List<String> engineFolders, LibraryBaseline baseline,
                         String engineVersion) {
        public Inputs {
            Objects.requireNonNull(project, "project");
            projectFiles = List.copyOf(projectFiles);
            projectFolders = List.copyOf(projectFolders);
            Objects.requireNonNull(engine, "engine");
            engineFolders = List.copyOf(engineFolders);
            Objects.requireNonNull(baseline, "baseline");
            Objects.requireNonNull(engineVersion, "engineVersion");
        }
    }

    private final List<FileDecision> decisions;
    private final List<TargetFile> files;
    private final List<String> folders;
    private final List<String> droppedSidecars;
    private final List<String> renamed;
    private final List<String> clashes;
    private final byte[] manifest;
    private final boolean upToDate;
    private final boolean keepMine;

    private LibraryPlan(List<FileDecision> decisions, List<TargetFile> files, List<String> folders,
                        List<String> droppedSidecars, List<String> renamed, List<String> clashes, byte[] manifest,
                        boolean upToDate, boolean keepMine) {
        this.decisions = List.copyOf(decisions);
        this.files = List.copyOf(files);
        this.folders = List.copyOf(folders);
        this.droppedSidecars = List.copyOf(droppedSidecars);
        this.renamed = List.copyOf(renamed);
        this.clashes = List.copyOf(clashes);
        this.manifest = manifest;
        this.upToDate = upToDate;
        this.keepMine = keepMine;
    }

    /**
     * The plan for {@code inputs}.
     *
     * @param keepMine whether a file in conflict keeps the user's copy instead of taking the engine's
     */
    public static LibraryPlan of(Inputs inputs, boolean keepMine) {
        Objects.requireNonNull(inputs, "inputs");
        List<FileDecision> decisions = LibraryClassifier.classify(inputs.project(), inputs.engine(),
                inputs.baseline());
        Spelling spelling = new Spelling(inputs.engineFolders(), inputs.engine(), inputs.projectFolders());

        // The library files.
        List<TargetFile> files = new ArrayList<>();
        Map<String, String> targetByKey = new HashMap<>();
        List<String> renamed = new ArrayList<>();
        boolean changesFiles = false;
        for (FileDecision decision : decisions) {
            Optional<Source> source = sourceOf(decision.verdict(), keepMine);
            if (source.isEmpty()) {
                changesFiles = true;
                continue;
            }
            String target = decision.enginePath() != null
                    ? decision.enginePath()
                    : spelling.respell(decision.projectPath());
            if (source.get() == Source.ENGINE) {
                changesFiles = true;
                String hash = inputs.engine().get(decision.key()).orElseThrow().hash();
                files.add(new TargetFile(target, Source.ENGINE, decision.enginePath(), hash));
            } else {
                String hash = inputs.project().get(decision.key()).orElseThrow().hash();
                files.add(new TargetFile(target, Source.PROJECT, decision.projectPath(), hash));
                if (!target.equals(decision.projectPath())) {
                    renamed.add(decision.projectPath());
                }
            }
            targetByKey.put(decision.key(), target);
        }

        // The sidecars follow their files.
        List<String> droppedSidecars = new ArrayList<>();
        for (String file : inputs.projectFiles()) {
            if (!LibraryManifest.isSidecar(file)) {
                continue;
            }
            int suffixStart = file.length() - LibraryManifest.SIDECAR_EXTENSION.length();
            String companionKey = LibraryManifest.pathKey(file.substring(0, suffixStart));
            boolean companionDecided = decisions.stream().anyMatch(decision -> decision.key().equals(companionKey));
            String target;
            if (companionDecided) {
                String companionTarget = targetByKey.get(companionKey);
                if (companionTarget == null) {
                    droppedSidecars.add(file);
                    continue;
                }
                target = companionTarget + file.substring(suffixStart);
            } else {
                target = spelling.respell(file);
            }
            files.add(new TargetFile(target, Source.PROJECT, file, null));
            if (!target.equals(file)) {
                renamed.add(file);
            }
        }
        files.sort(Comparator.comparing(TargetFile::path));

        List<String> folders = folders(inputs, files, spelling);
        // A file of one library where the other has a folder: the new library cannot hold both.
        Set<String> folderKeys = new TreeSet<>();
        folders.forEach(folder -> folderKeys.add(LibraryManifest.pathKey(folder)));
        List<String> clashes = files.stream().map(TargetFile::path)
                .filter(path -> folderKeys.contains(LibraryManifest.pathKey(path)))
                .toList();
        byte[] manifest = LibraryManifest.toJson(inputs.engineVersion(), inputs.engine().hashesByPath());

        boolean recordCurrent = inputs.baseline() instanceof LibraryBaseline.Recorded recorded
                && recorded.files().hashesByPath().equals(inputs.engine().hashesByPath());
        boolean upToDate = !changesFiles && droppedSidecars.isEmpty() && renamed.isEmpty() && clashes.isEmpty()
                && new TreeSet<>(folders).equals(new TreeSet<>(inputs.projectFolders())) && recordCurrent;
        return new LibraryPlan(decisions, files, folders, droppedSidecars, renamed, clashes, manifest, upToDate,
                keepMine);
    }

    /** Where a verdict's file comes from in the new library; empty when it is not in it. */
    static Optional<Source> sourceOf(Verdict verdict, boolean keepMine) {
        return switch (verdict) {
            case NOOP, KEEP_EDIT, KEEP_REMOVED_EDITED, USER_FILE -> Optional.of(Source.PROJECT);
            case ADD, RESTORE, UPDATE -> Optional.of(Source.ENGINE);
            case CONFLICT -> Optional.of(keepMine ? Source.PROJECT : Source.ENGINE);
            case DELETE -> Optional.empty();
        };
    }

    /** The new library's folders, sorted so that each parent comes before its children. */
    private static List<String> folders(Inputs inputs, List<TargetFile> files, Spelling spelling) {
        Map<String, String> byKey = new LinkedHashMap<>();
        for (TargetFile file : files) {
            addParents(file.path(), byKey);
        }
        for (String folder : inputs.engineFolders()) {
            addFolder(folder, byKey);
        }
        // An empty folder of the user's: one that held no file at all before the update.
        Set<String> heldFiles = new TreeSet<>();
        for (String file : inputs.projectFiles()) {
            addParentKeys(file, heldFiles);
        }
        for (String folder : inputs.projectFolders()) {
            if (!heldFiles.contains(LibraryManifest.pathKey(folder))) {
                addFolder(spelling.respell(folder), byKey);
            }
        }
        List<String> folders = new ArrayList<>(byKey.values());
        folders.sort(Comparator.naturalOrder());
        return folders;
    }

    private static void addParents(String path, Map<String, String> byKey) {
        int slash = path.lastIndexOf('/');
        if (slash > 0) {
            addFolder(path.substring(0, slash), byKey);
        }
    }

    /** Adds {@code folder} and every folder above it, keeping the first spelling met for each. */
    private static void addFolder(String folder, Map<String, String> byKey) {
        addParents(folder, byKey);
        byKey.putIfAbsent(LibraryManifest.pathKey(folder), folder);
    }

    private static void addParentKeys(String path, Collection<String> keys) {
        for (int slash = path.indexOf('/'); slash > 0; slash = path.indexOf('/', slash + 1)) {
            keys.add(LibraryManifest.pathKey(path.substring(0, slash)));
        }
    }

    /**
     * Spells a path the way the new library does: each folder as the engine spells it when the engine has it, else as
     * the project does; the last name as given.
     */
    private static final class Spelling {
        private final Map<String, String> engineNames = new HashMap<>();
        private final Map<String, String> projectNames = new HashMap<>();

        Spelling(List<String> engineFolders, LibrarySnapshot engine, List<String> projectFolders) {
            for (String folder : engineFolders) {
                remember(folder, engineNames);
            }
            for (LibrarySnapshot.Entry entry : engine.byKey().values()) {
                int slash = entry.relative().lastIndexOf('/');
                if (slash > 0) {
                    remember(entry.relative().substring(0, slash), engineNames);
                }
            }
            for (String folder : projectFolders) {
                remember(folder, projectNames);
            }
        }

        /** Records the last name of {@code folder} and of every folder above it, by the folder's key. */
        private static void remember(String folder, Map<String, String> names) {
            String[] parts = folder.split("/");
            StringBuilder prefix = new StringBuilder();
            for (String part : parts) {
                if (!prefix.isEmpty()) {
                    prefix.append('/');
                }
                prefix.append(part);
                names.putIfAbsent(LibraryManifest.pathKey(prefix.toString()), part);
            }
        }

        String respell(String path) {
            String[] parts = path.split("/");
            StringBuilder spelled = new StringBuilder();
            StringBuilder prefix = new StringBuilder();
            for (int i = 0; i < parts.length; i++) {
                if (i > 0) {
                    spelled.append('/');
                    prefix.append('/');
                }
                prefix.append(parts[i]);
                if (i == parts.length - 1) {
                    spelled.append(parts[i]);
                    break;
                }
                String key = LibraryManifest.pathKey(prefix.toString());
                String name = engineNames.get(key);
                spelled.append(name != null ? name : projectNames.getOrDefault(key, parts[i]));
            }
            return spelled.toString();
        }
    }

    /**
     * A fingerprint of everything a plan is made from: two previews of an unchanged project and engine have the same
     * one, so an update can tell that what the user confirmed is still what it would do.
     */
    public static String fingerprint(Inputs inputs) {
        Objects.requireNonNull(inputs, "inputs");
        StringBuilder text = new StringBuilder();
        text.append("project\n");
        inputs.project().hashesByPath().forEach((path, hash) -> text.append(path).append('\t').append(hash)
                .append('\n'));
        text.append("projectFiles\n");
        new TreeSet<>(inputs.projectFiles()).forEach(file -> text.append(file).append('\n'));
        text.append("projectFolders\n");
        new TreeSet<>(inputs.projectFolders()).forEach(folder -> text.append(folder).append('\n'));
        text.append("engine ").append(inputs.engineVersion()).append('\n');
        inputs.engine().hashesByPath().forEach((path, hash) -> text.append(path).append('\t').append(hash)
                .append('\n'));
        text.append("engineFolders\n");
        new TreeSet<>(inputs.engineFolders()).forEach(folder -> text.append(folder).append('\n'));
        switch (inputs.baseline()) {
            case LibraryBaseline.Recorded recorded -> {
                text.append("recorded ").append(recorded.engineVersion()).append('\n');
                recorded.files().hashesByPath().forEach((path, hash) -> text.append(path).append('\t').append(hash)
                        .append('\n'));
            }
            case LibraryBaseline.Legacy legacy -> text.append("legacy ").append(legacy.hashes().through())
                    .append('\n');
        }
        try {
            MessageDigest digest = MessageDigest.getInstance(LibraryManifest.HASH_ALGORITHM);
            return HexFormat.of().formatHex(digest.digest(text.toString().getBytes(StandardCharsets.UTF_8)));
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(LibraryManifest.HASH_ALGORITHM + " is not available", e);
        }
    }

    /** One decision per library file of either side, sorted by key. */
    public List<FileDecision> decisions() {
        return decisions;
    }

    /** The decisions with this verdict. */
    public List<FileDecision> decisions(Verdict verdict) {
        return decisions.stream().filter(decision -> decision.verdict() == verdict).toList();
    }

    /** How many decisions have each verdict (every verdict listed, 0 included). */
    public Map<Verdict, Integer> counts() {
        Map<Verdict, Integer> counts = new TreeMap<>();
        for (Verdict verdict : Verdict.values()) {
            counts.put(verdict, 0);
        }
        decisions.forEach(decision -> counts.merge(decision.verdict(), 1, Integer::sum));
        return counts;
    }

    /** Every file of the new library (library files and sidecars), sorted by path. */
    public List<TargetFile> files() {
        return files;
    }

    /** Every folder of the new library, '/'-relative, sorted so that each parent comes before its children. */
    public List<String> folders() {
        return folders;
    }

    /** The project's sidecars that go, because their file is deleted. */
    public List<String> droppedSidecars() {
        return droppedSidecars;
    }

    /** The project's files kept under a new spelling (the engine's), by their old path. */
    public List<String> renamed() {
        return renamed;
    }

    /**
     * The paths the new library would need both as a file and as a folder (a file of one library where the other has
     * a folder, ignoring case); such a plan cannot be built, and the update refuses it. Empty for a buildable plan.
     */
    public List<String> clashes() {
        return clashes;
    }

    /** The new {@code .vflib.json}'s content. */
    public byte[] manifest() {
        return manifest.clone();
    }

    /** Whether building the plan would change nothing: then the update writes nothing at all. */
    public boolean upToDate() {
        return upToDate;
    }

    /** Whether files in conflict keep the user's copy. */
    public boolean keepMine() {
        return keepMine;
    }
}
