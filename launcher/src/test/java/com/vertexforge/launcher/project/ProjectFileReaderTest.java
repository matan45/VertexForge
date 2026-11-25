package com.vertexforge.launcher.project;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.model.ProjectCreationResult;
import com.vertexforge.launcher.service.CreationMonitor;
import com.vertexforge.launcher.service.ProjectCreator;
import com.vertexforge.launcher.util.VersionNumber;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.MethodSource;
import org.junit.jupiter.params.provider.ValueSource;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.util.OptionalInt;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The exact messages below are Gson 2.13.0's (checked against the real library): a syntax error ends with
 * {@code " at line L column C path …"} and usually a {@code "See https://…"} line, and comments, single quotes,
 * unquoted names, {@code NaN} and data after the document all give only "Use JsonReader.setStrictness(…)".
 */
class ProjectFileReaderTest {

    /**
     * As the editor saves a project (ProjectSerialization.cpp: a fresh nlohmann::json, so keys in alphabetical order,
     * dump(2) through a text-mode stream, so CRLF, and no final newline), with members this launcher does not use.
     */
    static final String ENGINE_WRITTEN = "{\r\n"
            + "  \"engineVersion\": \"VertexForge 1.0.0\",\r\n"
            + "  \"fontFallbackChain\": [\r\n    \"fonts/Noto.vfFont\"\r\n  ],\r\n"
            + "  \"lastModified\": \"2026-11-17 10:00:00\",\r\n"
            + "  \"pluginApiVersion\": 25,\r\n"
            + "  \"projectName\": \"Engine Game\",\r\n"
            + "  \"schemaVersion\": \"1.0\",\r\n"
            + "  \"startupScene\": \"scenes/Main.vfScene\",\r\n"
            + "  \"version\": \"0.1.0\",\r\n"
            + "  \"workingDirectory\": \".\"\r\n"
            + "}";

    /** An error at line 3, column 8: the name "x" is not followed by a colon. */
    static final String MISSING_COLON_ON_LINE_3 = "{\n  \"projectName\": \"A\",\n  \"x\" 1\n}";

    @TempDir
    Path tempDir;

    private static InvalidProjectFileException refusal(String text) {
        return assertThrows(InvalidProjectFileException.class, () -> ProjectFileReader.parse(text));
    }

    private static InvalidProjectFileException readRefusal(Path file) {
        return assertThrows(InvalidProjectFileException.class, () -> ProjectFileReader.read(file));
    }

    private static void assertLocation(InvalidProjectFileException e, int line, int column) {
        assertEquals(OptionalInt.of(line), e.line(), e.getMessage());
        assertEquals(OptionalInt.of(column), e.column(), e.getMessage());
    }

    private static void assertNoLocation(InvalidProjectFileException e) {
        assertEquals(OptionalInt.empty(), e.line(), e.getMessage());
        assertEquals(OptionalInt.empty(), e.column(), e.getMessage());
    }

    @Test
    void aProjectTheLauncherCreatedReadsBack() throws Exception {
        Path parent = Files.createDirectory(tempDir.resolve("parent"));
        ProjectCreator creator = new ProjectCreator(TestSupport.fixedClock(),
                TestSupport.log(tempDir.resolve(".logs")).log);
        ProjectCreationResult created = creator.create(TestSupport.request("Tom & Jerry's Game", parent,
                TestSupport.engineContent(tempDir.resolve("engine"))), CreationMonitor.NONE);
        assertTrue(created.success(), created::errorMessage);

        ProjectFile project = ProjectFileReader.read(created.projectPath());

        assertEquals("Tom & Jerry's Game", project.projectName());
        assertEquals("1.0", project.schemaVersion());
        assertEquals("VertexForge 1.0.0", project.engineVersion());
        // The editor would open what the launcher creates (VK-1686).
        assertNull(project.structureProblem());
        assertEquals(new SchemaVersion(1, 0), project.schema().orElseThrow());
        // The template's working directory (VK-1689).
        assertEquals("assets", project.workingDirectory());
    }

    @Test
    void aProjectTheEditorSavedReadsBack() throws InvalidProjectFileException {
        ProjectFile project = ProjectFileReader.parse(ENGINE_WRITTEN);

        assertEquals("Engine Game", project.projectName());
        assertEquals("1.0", project.schemaVersion());
        assertEquals("VertexForge 1.0.0", project.engineVersion());
        assertNull(project.structureProblem());
        assertEquals(".", project.workingDirectory());
        // Saved before VK-1690: no engine has stamped its asset format yet.
        assertNull(project.assetFormatVersion());
    }

    @Test
    void theAssetFormatTheEditorStampsReadsBack() throws InvalidProjectFileException {
        ProjectFile project = ProjectFileReader.parse(engineWrittenWith("assetFormatVersion", "\"1.0.0\""));

        assertEquals("1.0.0", project.assetFormatVersion());
        assertNull(project.structureProblem());
    }

    /** A member of another type keeps its JSON text, which is never a version, so it reads as malformed (VK-1690). */
    @ParameterizedTest(name = "{0}")
    @ValueSource(strings = {"1", "1.5", "true", "{\"major\": 1}", "[1, 0, 0]"})
    void anAssetFormatOfAnotherTypeKeepsItsJsonText(String json) throws InvalidProjectFileException {
        ProjectFile project = ProjectFileReader.parse(engineWrittenWith("assetFormatVersion", json));

        assertEquals(JsonParser.parseString(json).toString(), project.assetFormatVersion());
        assertTrue(VersionNumber.parse(project.assetFormatVersion()).isEmpty(), project.assetFormatVersion());
    }

    @Test
    void anAssetFormatThatIsNullOrAbsentIsNotThere() throws InvalidProjectFileException {
        assertNull(ProjectFileReader.parse(engineWrittenWith("assetFormatVersion", "null")).assetFormatVersion());
        assertNull(ProjectFileReader.parse(engineWrittenWith("assetFormatVersion", null)).assetFormatVersion());
    }

    @Test
    void theWorkingDirectoryIsKeptAsWritten() throws InvalidProjectFileException {
        assertEquals("C:\\Games\\Shared Assets",
                ProjectFileReader.parse(engineWrittenWith("workingDirectory", "\"C:\\\\Games\\\\Shared Assets\""))
                        .workingDirectory());
        assertEquals("assets", ProjectFileReader.parse(engineWrittenWith("workingDirectory", "\"assets\""))
                .workingDirectory());
        // Of the wrong type it reads as missing, and the editor's own rule says why it would refuse it.
        ProjectFile wrongType = ProjectFileReader.parse(engineWrittenWith("workingDirectory", "42"));
        assertNull(wrongType.workingDirectory());
        assertEquals("Invalid project file: missing or invalid 'workingDirectory' field", wrongType.structureProblem());
    }

    @Test
    void onlyTheNameIsRequired() throws InvalidProjectFileException {
        ProjectFile project = ProjectFileReader.parse("{\"projectName\": \"Bare\"}");

        // The launcher reads it; the editor would refuse it, and says why (VK-1686).
        assertEquals(new ProjectFile("Bare", null, null,
                "Invalid project file: missing or invalid 'schemaVersion' field"), project);
        assertTrue(project.schema().isEmpty());
    }

    /** The editor's own project file, with one member replaced ({@code null} removes it). */
    private static String engineWrittenWith(String member, String json) {
        return withMember(ENGINE_WRITTEN, member, json);
    }

    /** {@code text} with one member replaced ({@code null} removes it). */
    private static String withMember(String text, String member, String json) {
        JsonObject root = JsonParser.parseString(text).getAsJsonObject();
        if (json == null) {
            root.remove(member);
        } else {
            root.add(member, JsonParser.parseString(json));
        }
        return root.toString();
    }

    static Stream<Arguments> editorRefusals() {
        return Stream.of(
                Arguments.of("schemaVersion", null, "Invalid project file: missing or invalid 'schemaVersion' field"),
                Arguments.of("schemaVersion", "1", "Invalid project file: missing or invalid 'schemaVersion' field"),
                Arguments.of("version", null, "Invalid project file: missing or invalid 'version' field"),
                Arguments.of("version", "null", "Invalid project file: missing or invalid 'version' field"),
                Arguments.of("workingDirectory", "[\".\"]",
                        "Invalid project file: missing or invalid 'workingDirectory' field"),
                Arguments.of("startupScene", null, "Invalid project file: missing or invalid 'startupScene' field"),
                Arguments.of("version", "\"\"", "Invalid project file: 'version' cannot be empty"),
                Arguments.of("workingDirectory", "\"\"", "Invalid project file: 'workingDirectory' cannot be empty"),
                Arguments.of("startupScene", "\"\"", "Invalid project file: 'startupScene' cannot be empty"));
    }

    @ParameterizedTest(name = "{0} = {1}")
    @MethodSource("editorRefusals")
    void whatTheEditorWouldRefuseIsReportedInItsWords(String member, String json, String problem)
            throws InvalidProjectFileException {
        ProjectFile project = ProjectFileReader.parse(engineWrittenWith(member, json));

        assertEquals("Engine Game", project.projectName());
        assertEquals(problem, project.structureProblem());
    }

    @Test
    void theEditorReportsTheFirstMemberItChecks() throws InvalidProjectFileException {
        // schemaVersion is checked before startupScene, and "is a string" before "is not empty".
        String text = engineWrittenWith("schemaVersion", null);
        text = withMember(text, "startupScene", "\"\"");

        assertEquals("Invalid project file: missing or invalid 'schemaVersion' field",
                ProjectFileReader.parse(text).structureProblem());
    }

    @Test
    void aSchemaVersionTheEditorCannotParseIsNotAStructureProblem() throws InvalidProjectFileException {
        // The members are fine; the version's text is judged by SchemaVersion (the editor's second check).
        ProjectFile project = ProjectFileReader.parse(engineWrittenWith("schemaVersion", "\"1.0.0\""));

        assertNull(project.structureProblem());
        assertTrue(project.schema().isEmpty());
        assertEquals("Invalid schema version format: '1.0.0' (expected MAJOR.MINOR)",
                EditorProjectRules.schemaFormatProblem(project.schemaVersion()));
    }

    @Test
    void versionsOfTheWrongTypeReadAsNull() throws InvalidProjectFileException {
        ProjectFile project = ProjectFileReader.parse(
                "{\"projectName\": \"A\", \"schemaVersion\": 1.0, \"engineVersion\": {\"major\": 1}}");

        assertNull(project.schemaVersion());
        assertNull(project.engineVersion());
    }

    @Test
    void aByteOrderMarkIsAccepted() throws Exception {
        assertEquals("Engine Game", ProjectFileReader.parse("﻿" + ENGINE_WRITTEN).projectName());

        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        bytes.write(new byte[]{(byte) 0xEF, (byte) 0xBB, (byte) 0xBF});
        bytes.write(ENGINE_WRITTEN.getBytes(StandardCharsets.UTF_8));
        Path file = Files.write(tempDir.resolve("Bom.vfproj"), bytes.toByteArray());

        assertEquals("Engine Game", ProjectFileReader.read(file).projectName());
    }

    @Test
    void invalidJsonSaysWhereAndWhy() {
        InvalidProjectFileException e = refusal(MISSING_COLON_ON_LINE_3);

        assertEquals("Not valid JSON at line 3, column 8: expected ':'", e.getMessage());
        assertEquals(e.getMessage(), e.reason());
        assertLocation(e, 3, 8);
    }

    @Test
    void aByteOrderMarkDoesNotMoveTheErrorsColumn() {
        assertLocation(refusal("﻿" + MISSING_COLON_ON_LINE_3), 3, 8);
        assertLocation(refusal("{ \"x\" 1 }"), 1, 8);
        assertLocation(refusal("﻿{ \"x\" 1 }"), 1, 8);
    }

    @Test
    void crlfLineEndsCountAsOneLineEach() {
        assertLocation(refusal(MISSING_COLON_ON_LINE_3.replace("\n", "\r\n")), 3, 8);
    }

    static Stream<Arguments> malformed() {
        return Stream.of(
                Arguments.of("a line comment", "{\n  // the name\n  \"projectName\": \"A\"\n}", 2, 4,
                        ProjectFileReader.NOT_STRICT_JSON),
                Arguments.of("a block comment", "{ /* x */ \"projectName\": \"A\" }", 1, 4,
                        ProjectFileReader.NOT_STRICT_JSON),
                Arguments.of("single quotes", "{\n  'projectName': \"A\"\n}", 2, 4,
                        ProjectFileReader.NOT_STRICT_JSON),
                Arguments.of("an unquoted name", "{\n  projectName: \"A\"\n}", 2, 4,
                        ProjectFileReader.NOT_STRICT_JSON),
                Arguments.of("a trailing comma in an object", "{\n  \"projectName\": \"A\",\n}", 3, 2,
                        "expected name"),
                Arguments.of("a trailing comma in an array", "{\"projectName\": \"A\", \"a\": [1,]}", 1, 31,
                        ProjectFileReader.NOT_STRICT_JSON),
                Arguments.of("NaN", "{\"projectName\": \"A\", \"x\": NaN}", 1, 27,
                        ProjectFileReader.NOT_STRICT_JSON),
                Arguments.of("data after the document", "{\"projectName\": \"A\"} x", 1, 23,
                        ProjectFileReader.MORE_DATA),
                Arguments.of("a second document", "{\"projectName\": \"A\"}\n{}", 2, 2,
                        ProjectFileReader.MORE_DATA),
                Arguments.of("a truncated file", "{\n  \"projectName\": \"A\"", 2, 21,
                        ProjectFileReader.ENDS_TOO_EARLY),
                Arguments.of("an unterminated string", "{\n  \"projectName\": \"A", 2, 20,
                        "unterminated string"));
    }

    @ParameterizedTest(name = "{0}")
    @MethodSource("malformed")
    void malformedJsonIsRefusedWithAPlaceAndAReadableReason(String what, String text, int line, int column,
                                                            String reason) {
        InvalidProjectFileException e = refusal(text);

        assertEquals("Not valid JSON at line " + line + ", column " + column + ": " + reason, e.getMessage(), what);
        assertLocation(e, line, column);
        assertFalse(e.getMessage().contains("https://"), e.getMessage());
        assertFalse(e.getMessage().contains("setStrictness"), e.getMessage());
        assertFalse(e.getMessage().contains("Exception"), e.getMessage());
    }

    @Test
    void tooDeepANestingIsRefusedWithAReason() {
        InvalidProjectFileException e = refusal("{\"projectName\": \"A\", \"x\": " + "[".repeat(300)
                + "]".repeat(300) + "}");

        assertTrue(e.getMessage().startsWith("Not valid JSON at line 1, column "), e.getMessage());
        assertTrue(e.getMessage().contains("nesting limit"), e.getMessage());
        assertFalse(e.getMessage().contains(" path $"), "Gson's path is not repeated");
    }

    @ParameterizedTest
    @ValueSource(strings = {"", "   \n ", "﻿"})
    void anEmptyFileIsRefused(String text) {
        InvalidProjectFileException e = refusal(text);

        assertEquals("The file is empty", e.getMessage());
        assertNoLocation(e);
    }

    @ParameterizedTest
    @ValueSource(strings = {"[]", "[{\"projectName\": \"A\"}]", "42", "\"Game\"", "true"})
    void aTopLevelThatIsNotAnObjectIsRefused(String text) {
        assertEquals("The file's top level is not a JSON object", refusal(text).getMessage());
    }

    @Test
    void aMissingProjectNameIsRefused() {
        assertEquals("The file has no \"projectName\"", refusal("{\"schemaVersion\": \"1.0\"}").getMessage());
        assertEquals("The file has no \"projectName\"", refusal("{\"projectName\": null}").getMessage());
    }

    @ParameterizedTest
    @ValueSource(strings = {"42", "true", "{\"name\": \"A\"}", "[\"A\"]"})
    void aProjectNameThatIsNotAStringIsRefused(String value) {
        assertEquals("Its \"projectName\" is not a string",
                refusal("{\"projectName\": " + value + "}").getMessage());
    }

    @ParameterizedTest
    @ValueSource(strings = {"", "   ", "\\t"})
    void aBlankProjectNameIsRefused(String value) {
        assertEquals("Its \"projectName\" is empty", refusal("{\"projectName\": \"" + value + "\"}").getMessage());
    }

    @Test
    void aFileThatIsNotUtf8IsRefused() throws IOException {
        byte[] bytes = "{\"projectName\": \"Café\"}".getBytes(StandardCharsets.ISO_8859_1);
        Path file = Files.write(tempDir.resolve("Latin1.vfproj"), bytes);

        InvalidProjectFileException e = readRefusal(file);

        assertEquals("The file is not valid UTF-8 text", e.getMessage());
        assertNoLocation(e);
    }

    @Test
    void aUnicodeNameReadsBack() throws Exception {
        String name = "משחק — 🎮";
        Path file = Files.writeString(tempDir.resolve("Unicode.vfproj"), "{\"projectName\": \"" + name + "\"}",
                StandardCharsets.UTF_8);

        assertEquals(name, ProjectFileReader.read(file).projectName());
    }

    @Test
    void aFileAtTheSizeLimitIsReadAndALargerOneIsRefused() throws Exception {
        String head = "{\"projectName\": \"Big\", \"padding\": \"";
        String tail = "\"}";
        int padding = ProjectFileReader.MAX_BYTES - head.length() - tail.length();
        Path atLimit = Files.writeString(tempDir.resolve("AtLimit.vfproj"), head + "x".repeat(padding) + tail);
        Path over = Files.writeString(tempDir.resolve("Over.vfproj"), head + "x".repeat(padding + 1) + tail);
        assertEquals(ProjectFileReader.MAX_BYTES, Files.size(atLimit));

        assertEquals("Big", ProjectFileReader.read(atLimit).projectName());
        InvalidProjectFileException e = readRefusal(over);
        assertTrue(e.getMessage().startsWith("The file is larger than 1 MiB"), e.getMessage());
        assertNoLocation(e);
    }

    @Test
    void aMissingFileIsAnIoError() {
        assertThrows(NoSuchFileException.class, () -> ProjectFileReader.read(tempDir.resolve("Gone.vfproj")));
    }
}
