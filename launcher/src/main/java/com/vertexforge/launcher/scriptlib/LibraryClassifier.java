package com.vertexforge.launcher.scriptlib;

import com.vertexforge.launcher.scriptlib.FileDecision.Verdict;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.TreeSet;

/**
 * Compares each library file three ways (VK-1691): the project's copy (P), the engine's copy (E) and the baseline (B,
 * one recorded hash; or K, every hash the launcher knows the path had). All hashes are normalised, so a file that
 * differs only in its line endings is never an edit. Pure.
 * <p>
 * With a recorded baseline:
 * <pre>
 *  P  E  B   condition        verdict
 *  -  e  -                    ADD
 *  -  e  b                    RESTORE (the user deleted it)
 *  -  -  b                    (nothing: gone on both sides)
 *  p  -  -                    USER_FILE
 *  p  -  b   p = b            DELETE
 *  p  -  b   p != b           KEEP_REMOVED_EDITED
 *  p  e  .   p = e            NOOP
 *  p  e  b   p = b != e       UPDATE
 *  p  e  b   p != b, e = b    KEEP_EDIT (only the user changed it: three-way merge keeps their side)
 *  p  e  .   otherwise        CONFLICT
 * </pre>
 * Without one (an older project, or a record that cannot be read), only whether {@code p} is a known version counts:
 * <pre>
 *  -  e      ADD;      p  e  p = e  NOOP;   p  e  p in K  UPDATE
 *  p  e  K = {e}  KEEP_EDIT (every known version is the engine's current one: only the user changed it)
 *  p  e  otherwise  CONFLICT
 *  p  -  p in K  DELETE;   p  -  otherwise  USER_FILE
 * </pre>
 */
public final class LibraryClassifier {

    private LibraryClassifier() {
        // Utility class - prevent instantiation
    }

    /** One decision for every file the project or the engine has, sorted by key. */
    public static List<FileDecision> classify(LibrarySnapshot project, LibrarySnapshot engine,
                                              LibraryBaseline baseline) {
        Objects.requireNonNull(project, "project");
        Objects.requireNonNull(engine, "engine");
        Objects.requireNonNull(baseline, "baseline");

        Set<String> keys = new TreeSet<>(project.byKey().keySet());
        keys.addAll(engine.byKey().keySet());
        List<FileDecision> decisions = new ArrayList<>(keys.size());
        for (String key : keys) {
            Optional<LibrarySnapshot.Entry> p = project.get(key);
            Optional<LibrarySnapshot.Entry> e = engine.get(key);
            Verdict verdict = switch (baseline) {
                case LibraryBaseline.Recorded recorded -> recorded(p.map(LibrarySnapshot.Entry::hash),
                        e.map(LibrarySnapshot.Entry::hash), recorded.hash(key));
                case LibraryBaseline.Legacy legacy -> legacy(p.map(LibrarySnapshot.Entry::hash),
                        e.map(LibrarySnapshot.Entry::hash), legacy.known(key));
            };
            decisions.add(new FileDecision(key, verdict, p.map(LibrarySnapshot.Entry::relative).orElse(null),
                    e.map(LibrarySnapshot.Entry::relative).orElse(null)));
        }
        return List.copyOf(decisions);
    }

    /** The recorded-baseline table. At least one of {@code p} and {@code e} is present. */
    static Verdict recorded(Optional<String> p, Optional<String> e, Optional<String> b) {
        if (p.isEmpty()) {
            return b.isPresent() ? Verdict.RESTORE : Verdict.ADD;
        }
        String project = p.get();
        if (e.isEmpty()) {
            if (b.isEmpty()) {
                return Verdict.USER_FILE;
            }
            return project.equals(b.get()) ? Verdict.DELETE : Verdict.KEEP_REMOVED_EDITED;
        }
        String engine = e.get();
        if (project.equals(engine)) {
            return Verdict.NOOP;
        }
        if (b.isPresent()) {
            String base = b.get();
            if (project.equals(base)) {
                return Verdict.UPDATE;
            }
            if (engine.equals(base)) {
                return Verdict.KEEP_EDIT;
            }
        }
        return Verdict.CONFLICT;
    }

    /** The table without a record. At least one of {@code p} and {@code e} is present. */
    static Verdict legacy(Optional<String> p, Optional<String> e, Set<String> known) {
        if (p.isEmpty()) {
            return Verdict.ADD;
        }
        String project = p.get();
        if (e.isEmpty()) {
            return known.contains(project) ? Verdict.DELETE : Verdict.USER_FILE;
        }
        String engine = e.get();
        if (project.equals(engine)) {
            return Verdict.NOOP;
        }
        if (known.contains(project)) {
            return Verdict.UPDATE;
        }
        // Every version the path ever had is the engine's current one: the engine never changed it, so only the user
        // did, and the three-way rule keeps their side, as with a recorded baseline (KEEP_EDIT).
        return known.equals(Set.of(engine)) ? Verdict.KEEP_EDIT : Verdict.CONFLICT;
    }
}
