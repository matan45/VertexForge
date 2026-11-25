package com.vertexforge.launcher.dialog;

import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Every dialog is owned by the window it is shown over and has the launcher look (VK-1694), because only
 * {@link DialogFactory} builds dialogs, message boxes and dialog windows. This guard reads the launcher's own source
 * and fails when any other class builds one itself, sets an owner, or changes a stylesheet list ({@code MainView} sets
 * the main window's).
 */
class DialogFactoryGuardTest {

    /** Surefire's working directory is launcher/. */
    private static final Path SOURCES = Path.of("src", "main", "java");
    private static final Set<String> FACTORY = Set.of("DialogFactory.java");
    private static final Set<String> STYLESHEET_OWNERS = Set.of("DialogFactory.java", "MainView.java");

    /** {@code new Alert(}, {@code new javafx.scene.control.Dialog<>()}, {@code new\n Stage()}, ... */
    static final Pattern BUILDS_A_DIALOG = Pattern.compile(
            "\\bnew\\s+(?:[\\w.]+\\.)?(Alert|Dialog|DialogPane|Stage|TextInputDialog|ChoiceDialog)\\b");
    static final Pattern SETS_AN_OWNER = Pattern.compile("\\.\\s*initOwner\\s*\\(");
    /** {@code getStylesheets().add(}, {@code .addAll(}, {@code .set(}, {@code .setAll(}. */
    static final Pattern CHANGES_A_STYLESHEET = Pattern.compile("getStylesheets\\s*\\(\\s*\\)\\s*\\.\\s*(add|set)");

    private record Source(Path file, String code) {
        String name() {
            return file.getFileName().toString();
        }
    }

    /** Every source file, with its comments blanked out (line breaks kept, so line numbers stay right). */
    private static List<Source> sources() throws IOException {
        assertTrue(Files.isDirectory(SOURCES), SOURCES.toAbsolutePath() + " not found");
        List<Source> sources = new ArrayList<>();
        try (Stream<Path> files = Files.walk(SOURCES)) {
            for (Path file : files.filter(path -> path.toString().endsWith(".java")).sorted().toList()) {
                sources.add(new Source(file, withoutComments(Files.readString(file, StandardCharsets.UTF_8))));
            }
        }
        return sources;
    }

    /** {@code text} with every comment replaced by spaces; string literals are kept as they are. */
    static String withoutComments(String text) {
        StringBuilder out = new StringBuilder(text.length());
        int i = 0;
        while (i < text.length()) {
            char c = text.charAt(i);
            char next = i + 1 < text.length() ? text.charAt(i + 1) : '\0';
            if (c == '/' && next == '/') {
                while (i < text.length() && text.charAt(i) != '\n') {
                    out.append(text.charAt(i) == '\r' ? '\r' : ' ');
                    i++;
                }
            } else if (c == '/' && next == '*') {
                int end = text.indexOf("*/", i + 2);
                int stop = end < 0 ? text.length() : end + 2;
                for (; i < stop; i++) {
                    char inside = text.charAt(i);
                    out.append(inside == '\n' || inside == '\r' ? inside : ' ');
                }
            } else if (c == '"' || c == '\'') {
                // A string or char literal (a text block's quotes are three of these): copied through to its end, so
                // a "//" inside it is not taken for a comment.
                char quote = c;
                out.append(c);
                i++;
                while (i < text.length() && text.charAt(i) != quote) {
                    if (text.charAt(i) == '\\' && i + 1 < text.length()) {
                        out.append(text.charAt(i)).append(text.charAt(i + 1));
                        i += 2;
                    } else {
                        out.append(text.charAt(i));
                        i++;
                    }
                }
                if (i < text.length()) {
                    out.append(quote);
                    i++;
                }
            } else {
                out.append(c);
                i++;
            }
        }
        return out.toString();
    }

    private static List<String> offenders(List<Source> sources, Pattern pattern, Set<String> allowed) {
        List<String> found = new ArrayList<>();
        for (Source source : sources) {
            if (allowed.contains(source.name())) {
                continue;
            }
            Matcher matcher = pattern.matcher(source.code());
            while (matcher.find()) {
                int line = 1 + (int) source.code().substring(0, matcher.start()).chars().filter(ch -> ch == '\n').count();
                found.add(SOURCES.relativize(source.file()) + ":" + line + ": " + matcher.group().strip());
            }
        }
        return found;
    }

    @Test
    void theGuardReadsTheLaunchersSource() throws IOException {
        List<Source> sources = sources();
        Source factory = sources.stream().filter(source -> source.name().equals("DialogFactory.java")).findFirst()
                .orElseThrow();
        Source mainView = sources.stream().filter(source -> source.name().equals("MainView.java")).findFirst()
                .orElseThrow();
        // The factory itself builds every kind it guards, so the patterns are known to match real code.
        assertTrue(BUILDS_A_DIALOG.matcher(factory.code()).find(), "the patterns no longer match the factory");
        assertTrue(SETS_AN_OWNER.matcher(factory.code()).find(), "the owner pattern no longer matches the factory");
        assertTrue(CHANGES_A_STYLESHEET.matcher(mainView.code()).find(),
                "the stylesheet pattern no longer matches MainView");
    }

    @Test
    void thePatternsCatchEveryWayOfWritingIt() {
        for (String code : List.of("new Alert(type)", "new javafx.scene.control.Alert(type)", "new Dialog<>()",
                "new Dialog()", "new\n    Stage()", "new DialogPane()", "new TextInputDialog(\"x\")")) {
            assertTrue(BUILDS_A_DIALOG.matcher(code).find(), code);
        }
        for (String code : List.of("new EnginesDialog(owner)", "new FileJobDialog(cancel, texts)",
                "new StageOption()", "new DialogFactoryThing()")) {
            assertTrue(!BUILDS_A_DIALOG.matcher(code).find(), code);
        }
        for (String code : List.of("pane.getStylesheets().add(x)", "scene.getStylesheets().setAll(x)",
                "pane.getStylesheets()\n        .addAll(x)")) {
            assertTrue(CHANGES_A_STYLESHEET.matcher(code).find(), code);
        }
        assertTrue(SETS_AN_OWNER.matcher("dialog\n    .initOwner(owner)").find());
    }

    @Test
    void commentsAreIgnoredButLineNumbersKept() {
        String code = "a // new Alert(x)\r\n/* new Stage()\n */ b \"new Alert(\" c '\"' d // new Dialog<>()";

        String stripped = withoutComments(code);

        assertEquals(code.length(), stripped.length());
        assertEquals(code.chars().filter(ch -> ch == '\n').count(), stripped.chars().filter(ch -> ch == '\n').count());
        assertTrue(!stripped.contains("Stage") && stripped.contains("\"new Alert(\""), stripped);
        assertTrue(!stripped.contains("Dialog"), "a quote in a char literal must not hide a comment: " + stripped);
    }

    @Test
    void onlyTheFactoryBuildsDialogsAndWindows() throws IOException {
        List<String> found = offenders(sources(), BUILDS_A_DIALOG, FACTORY);
        assertTrue(found.isEmpty(), "Build dialogs, message boxes and windows with DialogFactory:\n"
                + String.join("\n", found));
    }

    @Test
    void onlyTheFactorySetsAnOwner() throws IOException {
        List<String> found = offenders(sources(), SETS_AN_OWNER, FACTORY);
        assertTrue(found.isEmpty(), "Pass the owner to DialogFactory instead:\n" + String.join("\n", found));
    }

    @Test
    void onlyTheFactoryAndTheMainViewChangeAStylesheetList() throws IOException {
        List<String> found = offenders(sources(), CHANGES_A_STYLESHEET, STYLESHEET_OWNERS);
        assertTrue(found.isEmpty(), "Style dialog panes with DialogFactory:\n" + String.join("\n", found));
    }
}
