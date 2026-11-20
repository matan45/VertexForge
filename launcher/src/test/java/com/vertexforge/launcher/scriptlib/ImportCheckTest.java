package com.vertexforge.launcher.scriptlib;

import com.vertexforge.launcher.template.LibraryManifest;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.function.Predicate;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

/**
 * {@link ImportCheck} (VK-1691), the twin of {@code VFEngine/tests/test_repo_scan_helpers.hpp}'s
 * {@code stripComments}/{@code importResolves}. Pure: the "file system" outside the library is a predicate, and nothing
 * is read from the disk.
 */
class ImportCheckTest {

    /** An absolute scripts folder that never exists on disk. */
    private static final Path SCRIPTS = Path.of("").toAbsolutePath().getRoot()
            .resolve("vf-import-check-never-on-disk").resolve("assets").resolve("scripts");
    private static final Path LIBRARY = SCRIPTS.resolve("lib");
    private static final Path MAIN = SCRIPTS.resolve("game").resolve("Main.mt");

    private static Set<String> keys(String... paths) {
        Set<String> keys = new HashSet<>();
        for (String path : paths) {
            keys.add(LibraryManifest.pathKey(path));
        }
        return keys;
    }

    private static ImportCheck.Libraries libraries(List<String> searchPaths, Set<String> before,
                                                   Collection<String> after, Predicate<Path> elsewhere) {
        return new ImportCheck.Libraries(SCRIPTS, LIBRARY, searchPaths, before, after, elsewhere);
    }

    private static ImportCheck.Libraries libraries(Set<String> before, Collection<String> after) {
        return libraries(List.of("lib"), before, after, path -> false);
    }

    private static ImportCheck.Script main(String code) {
        return new ImportCheck.Script(MAIN, "game/Main.mt", code);
    }

    private static List<ImportCheck.BrokenImport> check(ImportCheck.Libraries libraries, ImportCheck.Script... scripts) {
        return ImportCheck.check(List.of(scripts), libraries);
    }

    // -----------------------------------------------------------------------------------------------------------------
    // stripComments
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void lineCommentsGoButTheLineBreakStays() {
        assertEquals("a \nb", ImportCheck.stripComments("a // gone\nb"));
        assertEquals("x", ImportCheck.stripComments("x// to the end"));
    }

    @Test
    void blockCommentsGoButTheirLineBreaksStay() {
        assertEquals("a \n\n b", ImportCheck.stripComments("a /* one\ntwo\n three */ b"));
        assertEquals("ab", ImportCheck.stripComments("a/**/b"));
        assertEquals("a", ImportCheck.stripComments("a/* never closed"));
        // A block comment does not nest, and "*/" outside one is code.
        assertEquals("a b */", ImportCheck.stripComments("a /* /* x */b */"));
    }

    @Test
    void commentMarkersInsideStringsAndCharactersStay() {
        assertEquals("s = \"// not a comment\"; ", ImportCheck.stripComments("s = \"// not a comment\"; // one"));
        assertEquals("s = \"/* nor this */\"", ImportCheck.stripComments("s = \"/* nor this */\""));
        assertEquals("c = '/'; ", ImportCheck.stripComments("c = '/'; // gone"));
        assertEquals("c = '\"'; ", ImportCheck.stripComments("c = '\"'; // a quote in a char starts no string"));
    }

    @Test
    void anEscapedQuoteDoesNotEndTheString() {
        assertEquals("s = \"a\\\" // still the string\" ",
                ImportCheck.stripComments("s = \"a\\\" // still the string\" // gone"));
        assertEquals("c = '\\'' ", ImportCheck.stripComments("c = '\\'' // gone"));
        assertEquals("s = \"a\\\\\" ", ImportCheck.stripComments("s = \"a\\\\\" // an escaped backslash ends it"));
    }

    @Test
    void anUnclosedStringKeepsTheRestOfTheText() {
        assertEquals("s = \"open // kept", ImportCheck.stripComments("s = \"open // kept"));
    }

    // -----------------------------------------------------------------------------------------------------------------
    // imports
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void everyImportIsFoundInOrder() {
        assertEquals(List.of("engine/Log.mt", "engine/Entity.mt", "x.mt"), ImportCheck.imports("""
                import * from "engine/Log.mt";
                import {Entity} from "engine/Entity.mt";
                import   A   from   "x.mt"; import B;
                """));
    }

    @Test
    void anImportMaySpanLines() {
        assertEquals(List.of("ui/Widgets.mt"), ImportCheck.imports("import {\n    Button,\n    Label\n} from \"ui/Widgets.mt\";"));
        assertEquals(List.of("ui/Widgets.mt"), ImportCheck.imports("import {\r\n  A,\r\n  B\r\n}\r\nfrom\r\n\"ui/Widgets.mt\";"));
    }

    @Test
    void commentedOutImportsAreNotTaken() {
        assertEquals(List.of("kept.mt"), ImportCheck.imports("""
                // import * from "line.mt";
                /* import * from "block.mt";
                   import * from "block2.mt"; */
                import * from /* inline */ "kept.mt";
                """));
    }

    @Test
    void anImportNeedsAQuotedTargetAndNoSemicolonBeforeFrom() {
        assertEquals(List.of(), ImportCheck.imports("import * from engine/Log.mt;"));
        assertEquals(List.of(), ImportCheck.imports("import A; from \"x.mt\";"));
        assertEquals(List.of(), ImportCheck.imports("import * from \"\";"));
        assertEquals(List.of(), ImportCheck.imports("imports * from"));
    }

    // -----------------------------------------------------------------------------------------------------------------
    // check: where an import is looked for
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void anImportFoundThroughTheSearchPathIsBrokenWhenTheUpdateRemovesItsFile() {
        ImportCheck.Libraries libraries = libraries(keys("engine/Log.mt", "engine/Entity.mt"), List.of("engine/Log.mt"));

        List<ImportCheck.BrokenImport> broken = check(libraries,
                main("import * from \"engine/Log.mt\";\nimport * from \"engine/Entity.mt\";"));

        assertEquals(List.of(new ImportCheck.BrokenImport("game/Main.mt", "engine/Entity.mt", null)), broken);
    }

    @Test
    void anImportFoundThroughTheScriptsFolderIsCheckedToo() {
        ImportCheck.Libraries libraries = libraries(List.of(), keys("engine/Log.mt"), List.of(), path -> false);

        assertEquals(List.of(new ImportCheck.BrokenImport("game/Main.mt", "lib/engine/Log.mt", null)),
                check(libraries, main("import * from \"lib/engine/Log.mt\";")));
    }

    @Test
    void anImportFoundBesideTheImportingFileIsCheckedToo() {
        Path helper = LIBRARY.resolve("mine").resolve("Helper.mt");
        ImportCheck.Script script = new ImportCheck.Script(helper, "lib/mine/Helper.mt",
                "import * from \"Other.mt\";\nimport * from \"../engine/Log.mt\";");
        ImportCheck.Libraries libraries = libraries(keys("mine/Helper.mt", "mine/Other.mt", "engine/Log.mt"),
                List.of("mine/Helper.mt", "engine/Log.mt"));

        assertEquals(List.of(new ImportCheck.BrokenImport("lib/mine/Helper.mt", "Other.mt", null)),
                check(libraries, script));
    }

    @Test
    void withoutTheSearchPathALibraryImportNeverResolvedSoIsNotReported() {
        ImportCheck.Libraries libraries = libraries(List.of(), keys("engine/Log.mt"), List.of(), path -> false);

        assertEquals(List.of(), check(libraries, main("import * from \"engine/Log.mt\";")));
    }

    @Test
    void anImportThatIsAlreadyBrokenIsNotReported() {
        ImportCheck.Libraries libraries = libraries(keys("engine/Log.mt"), List.of());

        assertEquals(List.of(), check(libraries, main("import * from \"engine/Typo.mt\";")));
    }

    @Test
    void anImportStillFoundOutsideTheLibraryIsNotBroken() {
        Path shadow = SCRIPTS.resolve("game").resolve("engine").resolve("Log.mt");
        ImportCheck.Libraries libraries = libraries(List.of("lib"), keys("engine/Log.mt"), List.of(),
                path -> path.equals(shadow));

        assertEquals(List.of(), check(libraries, main("import * from \"engine/Log.mt\";")),
                "game/engine/Log.mt, beside the importing file, still answers it");
    }

    @Test
    void anImportTheUpdateKeepsIsNotBroken() {
        ImportCheck.Libraries libraries = libraries(keys("engine/Log.mt"), List.of("engine/Log.mt"));

        assertEquals(List.of(), check(libraries, main("import * from \"engine/Log.mt\";")));
    }

    @Test
    void theLibraryIsComparedIgnoringCase() {
        ImportCheck.Libraries libraries = libraries(keys("Engine/LOG.mt"), List.of("engine/log.MT"));

        assertEquals(List.of(), check(libraries, main("import * from \"ENGINE/Log.mt\";")));
    }

    @Test
    void severalSearchPathsAreEachTried() {
        ImportCheck.Libraries libraries = libraries(List.of("vendor", "lib"), keys("engine/Log.mt"), List.of(),
                path -> false);

        assertEquals(1, check(libraries, main("import * from \"engine/Log.mt\";")).size());
    }

    @Test
    void anAbsoluteTargetIsTakenAsItIs() {
        String absolute = LIBRARY.resolve("engine").resolve("Log.mt").toString();
        ImportCheck.Libraries libraries = libraries(keys("engine/Log.mt"), List.of());

        // The target is the text between the quotes, as the C++ twin's regex takes it.
        assertEquals(List.of(new ImportCheck.BrokenImport("game/Main.mt", absolute, null)),
                check(libraries, main("import * from \"" + absolute + "\";")));
    }

    @Test
    void anAbsoluteTargetOutsideTheLibraryAsksTheFileSystem() {
        Path outside = SCRIPTS.getParent().resolve("shared").resolve("Util.mt");
        ImportCheck.Libraries gone = libraries(List.of("lib"), keys(), List.of(), path -> false);
        ImportCheck.Libraries there = libraries(List.of("lib"), keys(), List.of(), path -> path.equals(outside));

        ImportCheck.Script script = main("import * from \"" + outside + "\";");
        assertEquals(List.of(), check(gone, script), "never resolved");
        assertEquals(List.of(), check(there, script), "resolves before and after");
    }

    @Test
    void anInvalidTargetOrSearchPathFindsNothingAndThrowsNothing() {
        ImportCheck.Libraries libraries = libraries(List.of("<bad>", "lib"), keys("engine/Log.mt"), List.of(),
                path -> false);

        List<ImportCheck.BrokenImport> broken = check(libraries,
                main("import * from \"a<b>.mt\";\nimport * from \"x\u0000y\";\nimport * from \"engine/Log.mt\";"));

        assertEquals(List.of(new ImportCheck.BrokenImport("game/Main.mt", "engine/Log.mt", null)), broken);
    }

    @Test
    void brokenImportsAreReportedPerScriptInOrder() {
        ImportCheck.Libraries libraries = libraries(keys("a.mt", "b.mt"), List.of());
        ImportCheck.Script second = new ImportCheck.Script(SCRIPTS.resolve("game").resolve("Second.mt"),
                "game/Second.mt", "import * from \"a.mt\";");

        List<ImportCheck.BrokenImport> broken = check(libraries,
                main("import * from \"b.mt\";\nimport * from \"a.mt\";"), second);

        assertEquals(List.of("game/Main.mt b.mt", "game/Main.mt a.mt", "game/Second.mt a.mt"),
                broken.stream().map(b -> b.script() + " " + b.target()).toList());
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Suggestions
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void theOnlyFileOfThatNameInTheNewLibraryIsSuggested() {
        ImportCheck.Libraries libraries = libraries(keys("engine/Log.mt"),
                List.of("engine/util/log.MT", "engine/util/log.MT.vfmeta", "core/Array.mt"));

        assertEquals(List.of(new ImportCheck.BrokenImport("game/Main.mt", "engine/Log.mt", "engine/util/log.MT")),
                check(libraries, main("import * from \"engine/Log.mt\";")));
    }

    @Test
    void twoFilesOfThatNameSuggestNothing() {
        ImportCheck.Libraries libraries = libraries(keys("engine/Log.mt"),
                List.of("engine/util/Log.mt", "debug/Log.mt"));

        assertNull(check(libraries, main("import * from \"engine/Log.mt\";")).getFirst().suggestion());
    }

    @Test
    void aBackslashTargetIsSuggestedByItsLastName() {
        ImportCheck.Libraries libraries = libraries(keys("engine/Log.mt"), List.of("engine/util/Log.mt"));

        List<ImportCheck.BrokenImport> broken = check(libraries, main("import * from \"engine\\Log.mt\";"));

        assertEquals("engine/util/Log.mt", broken.getFirst().suggestion());
    }

    // -----------------------------------------------------------------------------------------------------------------
    // relativeTo
    // -----------------------------------------------------------------------------------------------------------------

    @Test
    void aPathBelowTheFolderIsRelativeIgnoringCase() {
        Path folder = SCRIPTS.resolve("Lib");
        Path inside = Path.of(SCRIPTS.toString().toUpperCase(java.util.Locale.ROOT)).resolve("LIB").resolve("Engine")
                .resolve("Log.mt");

        assertEquals(Optional.of("Engine/Log.mt"), ImportCheck.relativeTo(folder, inside));
    }

    @Test
    void theFolderItselfAndPathsOutsideItAreNotRelative() {
        assertEquals(Optional.empty(), ImportCheck.relativeTo(LIBRARY, LIBRARY));
        assertEquals(Optional.empty(), ImportCheck.relativeTo(LIBRARY, SCRIPTS));
        assertEquals(Optional.empty(), ImportCheck.relativeTo(LIBRARY, SCRIPTS.resolve("libs").resolve("x.mt")));
        assertEquals(Optional.empty(), ImportCheck.relativeTo(LIBRARY, SCRIPTS.resolve("game").resolve("x.mt")));
        assertEquals(Optional.empty(), ImportCheck.relativeTo(LIBRARY, Path.of("lib", "x.mt")), "a relative path");
    }
}
