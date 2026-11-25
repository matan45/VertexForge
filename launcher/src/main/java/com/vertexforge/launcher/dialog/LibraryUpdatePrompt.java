package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.scriptlib.FileDecision;
import com.vertexforge.launcher.scriptlib.ImportCheck;
import com.vertexforge.launcher.scriptlib.LibraryBaseline;
import com.vertexforge.launcher.scriptlib.LibraryPlan;
import com.vertexforge.launcher.service.ScriptLibraryUpdater;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/**
 * What the script-library update shows before it changes anything (VK-1691): every file it would touch, grouped by
 * what happens to it, the imports it would break, and where the backup goes. Toolkit-free, so the update flow builds
 * it in plain unit tests; {@link LibraryUpdateDialog} only shows it.
 *
 * @param title     the window's title
 * @param header    one line: what is about to happen
 * @param intro     a short summary
 * @param groups    the files, by what happens to them; only groups with files
 * @param conflicts how many files both the user and the engine changed (the "keep mine" choice is about them)
 * @param warnings  imports the update would break
 * @param notes     what else the user should know (the backup, the baseline, leftovers)
 */
public record LibraryUpdatePrompt(String title, String header, String intro, List<Group> groups, int conflicts,
                                  List<String> warnings, List<String> notes) {

    public static final String TITLE = "Update Script Library";
    /** The "keep mine" choice's label. */
    public static final String KEEP_MINE = "Keep my version of the files I changed";

    /**
     * Files that share a fate.
     *
     * @param label       what happens to them
     * @param explanation why
     * @param files       their paths in the library
     */
    public record Group(String label, String explanation, List<String> files) {
        public Group {
            Objects.requireNonNull(label, "label");
            Objects.requireNonNull(explanation, "explanation");
            files = List.copyOf(files);
        }
    }

    public LibraryUpdatePrompt {
        Objects.requireNonNull(title, "title");
        Objects.requireNonNull(header, "header");
        Objects.requireNonNull(intro, "intro");
        groups = List.copyOf(groups);
        warnings = List.copyOf(warnings);
        notes = List.copyOf(notes);
    }

    /**
     * The prompt for {@code preview}.
     *
     * @param name        the project's name
     * @param engineLabel the engine as the window names it
     * @param changed     why the question is asked again (the library changed since the last preview), or
     *                    {@code null}
     */
    public static LibraryUpdatePrompt of(String name, String engineLabel, ScriptLibraryUpdater.Preview preview,
                                         String changed) {
        Objects.requireNonNull(name, "name");
        Objects.requireNonNull(engineLabel, "engineLabel");
        Objects.requireNonNull(preview, "preview");
        LibraryPlan plan = preview.plan();

        List<Group> groups = new ArrayList<>();
        add(groups, plan, FileDecision.Verdict.UPDATE, "Updated",
                "The engine changed these files and you did not: the engine's copies replace them.");
        add(groups, plan, FileDecision.Verdict.ADD, "Added", "New in the engine's library.");
        add(groups, plan, FileDecision.Verdict.RESTORE, "Restored",
                "These were deleted from the project's library: the engine's copies are put back.");
        add(groups, plan, FileDecision.Verdict.DELETE, "Removed",
                "The engine's library no longer has these, and you never changed them.");
        add(groups, plan, FileDecision.Verdict.CONFLICT, "Changed by you and by the engine",
                "The engine's copies replace these; your copies stay in the backup. Tick \"" + KEEP_MINE
                        + "\" to keep yours instead.");
        add(groups, plan, FileDecision.Verdict.KEEP_EDIT, "Changed by you (kept)",
                "Only you changed these; the engine has nothing new for them, so they stay as they are.");
        add(groups, plan, FileDecision.Verdict.KEEP_REMOVED_EDITED, "Removed by the engine, changed by you (kept)",
                "The engine's library no longer has these, but you changed them, so they stay.");
        add(groups, plan, FileDecision.Verdict.USER_FILE, "Your own files (kept)",
                "The engine's library has never had these; they stay.");
        if (!plan.renamed().isEmpty()) {
            groups.add(new Group("Renamed", "Spelled as the engine spells them (only the letter case changes).",
                    plan.renamed()));
        }

        int upToDate = plan.decisions(FileDecision.Verdict.NOOP).size();
        String intro = (preview.hadLibrary()
                ? upToDate + (upToDate == 1 ? " file is" : " files are") + " already up to date."
                : "The project has no script library yet: the engine's is added.")
                + (changed == null ? "" : " " + changed);

        List<String> warnings = new ArrayList<>();
        for (ImportCheck.BrokenImport broken : preview.brokenImports()) {
            warnings.add(broken.script() + " imports \"" + broken.target() + "\", which will no longer be found"
                    + (broken.suggestion() == null ? "." : " (the library now has " + broken.suggestion() + ")."));
        }

        List<String> notes = new ArrayList<>();
        if (preview.baseline() instanceof LibraryBaseline.Legacy legacy) {
            notes.add(legacy.note());
        }
        if (preview.hadLibrary()) {
            notes.add("The library is backed up into " + preview.backupFolder() + " first.");
        }
        notes.add("Only " + preview.library() + " changes; the game's scripts are not touched.");
        notes.addAll(preview.notes());

        return new LibraryUpdatePrompt(TITLE, "Update the script library of \"" + name + "\" to " + engineLabel + "?",
                intro, groups, preview.conflicts(), warnings, notes);
    }

    private static void add(List<Group> groups, LibraryPlan plan, FileDecision.Verdict verdict, String label,
                            String explanation) {
        List<String> files = plan.decisions(verdict).stream().map(FileDecision::displayPath).toList();
        if (!files.isEmpty()) {
            groups.add(new Group(label + " (" + files.size() + ")", explanation, files));
        }
    }
}
