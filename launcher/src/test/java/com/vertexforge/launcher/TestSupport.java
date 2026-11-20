package com.vertexforge.launcher;

import com.vertexforge.launcher.engine.EngineContent;
import com.vertexforge.launcher.env.DriveTypes;
import com.vertexforge.launcher.env.LauncherEnvironment;
import com.vertexforge.launcher.env.ProcessStarter;
import com.vertexforge.launcher.service.CreateRequest;
import com.vertexforge.launcher.template.Placeholders;
import com.vertexforge.launcher.template.ProjectBlueprint;
import com.vertexforge.launcher.template.TemplateException;
import com.vertexforge.launcher.template.TemplateManifest;
import com.vertexforge.launcher.util.LauncherLog;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.PrintStream;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Clock;
import java.time.Instant;
import java.time.ZoneId;
import java.time.ZoneOffset;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.TreeMap;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/** Shared fakes for the launcher's unit tests. Nothing here touches the real user folder. */
public final class TestSupport {

    public static final Instant T0 = Instant.parse("2026-01-02T03:04:05Z");

    /** The registered id of the engine {@link #engineContent} describes. */
    public static final UUID FAKE_ENGINE_ID = UUID.fromString("6f1c2a5e-8d3b-4c7a-9e21-0b5d7f3a1c88");
    /** The fake engine's version. */
    public static final String FAKE_ENGINE_VERSION = "1.0.0";
    /** The template every fake engine has (VK-1689). */
    public static final String TEMPLATE = "empty";
    /** The files of the fake engine's script library, relative to it, with their content (one with CRLF). */
    public static final Map<String, String> FAKE_LIBRARY = fakeLibrary();
    /** The fake template's {@code project/} files, relative to the project folder, before the name is filled in. */
    public static final Map<String, String> FAKE_PAYLOAD = fakePayload();

    private static Map<String, String> fakeLibrary() {
        Map<String, String> files = new LinkedHashMap<>();
        files.put("core/Array.mt", "public class Array {\r\n}\r\n");
        files.put("engine/Log.mt", "public class Log {\n}\n");
        files.put("engine/oop/Behaviour.mt", "public class Behaviour {\n}\n");
        return java.util.Collections.unmodifiableMap(files);
    }

    private static Map<String, String> fakePayload() {
        Map<String, String> files = new LinkedHashMap<>();
        files.put(".gitignore", "/.vertexforge/\n/assets/scripts/compiled/\n");
        files.put("assets/scripts/scripts.mtproj", "<Project Name=\"{{PROJECT_NAME}}\" Version=\"1.0.0\">\n"
                + "  <Output Directory=\"compiled\" />\n</Project>\n");
        files.put("assets/scripts/game/Main.mt", "@Script\npublic class Main {\n"
                + "    string greeting = \"Hello from {{PROJECT_NAME}}!\";\n}\n");
        return java.util.Collections.unmodifiableMap(files);
    }

    private TestSupport() {
    }

    public static Clock fixedClock() {
        return Clock.fixed(T0, ZoneOffset.UTC);
    }

    /** A log in {@code dir} whose console output is captured. */
    public static CapturedLog log(Path dir) {
        return new CapturedLog(dir.resolve(LauncherLog.FILE_NAME), LauncherLog.DEFAULT_MAX_BYTES);
    }

    /**
     * The real path of a test folder. The CI runner's temp folder is an 8.3 short path ({@code RUNNER~1}), while
     * created projects are listed by their real path, so every comparison must start from the real one.
     */
    public static Path real(Path path) {
        try {
            return path.toRealPath();
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    public static boolean isWindows() {
        return System.getProperty("os.name", "").toLowerCase(Locale.ROOT).startsWith("windows");
    }

    /**
     * A drive letter (Z down to D) that does not exist on this machine right now, or empty when there is none or this
     * is not Windows. A and B are skipped: virtual floppy drives stall on some machines.
     */
    public static Optional<Path> absentDriveRoot() {
        if (!isWindows()) {
            return Optional.empty();
        }
        Set<String> roots = Arrays.stream(File.listRoots())
                .map(root -> root.getPath().toUpperCase(Locale.ROOT))
                .collect(Collectors.toSet());
        for (char letter = 'Z'; letter >= 'D'; letter--) {
            String root = letter + ":\\";
            if (!roots.contains(root) && !Files.exists(Path.of(root))) {
                return Optional.of(Path.of(root));
            }
        }
        return Optional.empty();
    }

    /**
     * A {@code .vfproj} the editor accepts (VK-1686: every member its {@code structureError} requires), called
     * {@code name}, with the given schema version and engine version.
     */
    public static String projectJson(String name, String schemaVersion, String engineVersion) {
        return """
                {
                  "schemaVersion": "%s",
                  "projectName": "%s",
                  "version": "0.1.0",
                  "workingDirectory": ".",
                  "startupScene": "scenes/Main.vfScene",
                  "engineVersion": "%s"
                }
                """.formatted(schemaVersion, name, engineVersion);
    }

    /** {@code folder/<name>.vfproj} holding {@link #projectJson} for schema 1.0 and VertexForge 1.0.0. */
    public static Path writeProject(Path folder, String name) {
        try {
            return Files.writeString(Files.createDirectories(folder).resolve(name + ".vfproj"),
                    projectJson(name, "1.0", "VertexForge 1.0.0"), StandardCharsets.UTF_8);
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    /**
     * Every file and folder under {@code root} (itself included) with its size, modification time and content hash, so
     * that two snapshots are equal only when nothing was created, deleted or changed (VK-1686).
     */
    public static Map<String, String> treeSnapshot(Path root) {
        try (Stream<Path> paths = Files.walk(root)) {
            Map<String, String> snapshot = new TreeMap<>();
            for (Path path : paths.toList()) {
                String key = root.relativize(path).toString();
                if (Files.isDirectory(path)) {
                    snapshot.put(key, "folder " + Files.getLastModifiedTime(path));
                } else {
                    byte[] bytes = Files.readAllBytes(path);
                    snapshot.put(key, bytes.length + " " + Files.getLastModifiedTime(path) + " "
                            + Arrays.hashCode(bytes));
                }
            }
            return snapshot;
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    /** The names of the files directly in {@code dir}. */
    public static Set<String> fileNames(Path dir) {
        try (Stream<Path> files = Files.list(dir)) {
            return files.map(path -> path.getFileName().toString()).collect(Collectors.toSet());
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    /**
     * An environment rooted in temp folders, with the given environment variables and process starter, a UTF-8 code
     * page (no code-page warnings), no drive information (no native calls) and no launcher exe (not packaged).
     */
    public static LauncherEnvironment environment(Path configDir, Clock clock, Map<String, String> variables,
                                                  ProcessStarter starter) {
        return environment(configDir, Optional.empty(), clock, variables, starter);
    }

    /** As {@link #environment(Path, Clock, Map, ProcessStarter)}, for a launcher started from this exe. */
    public static LauncherEnvironment environment(Path configDir, Optional<Path> launcherExecutable, Clock clock,
                                                  Map<String, String> variables, ProcessStarter starter) {
        return new LauncherEnvironment(clock, configDir, launcherExecutable, variables::get, starter,
                StandardCharsets.UTF_8, DriveTypes.unknown());
    }

    /**
     * A fake {@code Editor.exe} in {@code dir}. On Windows every file a test creates can be "executed" (its ACL
     * allows it, and {@code setExecutable} does nothing), so it passes {@code Files.isExecutable}.
     */
    public static Path fakeEditor(Path dir) {
        try {
            Path editor = Files.createDirectories(dir).resolve("Editor.exe");
            Files.writeString(editor, "fake");
            editor.toFile().setExecutable(true);
            return editor;
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    /**
     * An {@code engine.json} in {@code dir}, as {@code tools/engine_version.lua} writes it (VK-1680), with the given
     * configuration and working directory (relative to {@code dir}).
     */
    public static Path writeManifest(Path dir, String configuration, String workingDirectory) {
        return writeManifest(dir, configuration, workingDirectory, null);
    }

    /** As {@link #writeManifest(Path, String, String)}, with a {@code launchContract} member (VK-1683) when given. */
    public static Path writeManifest(Path dir, String configuration, String workingDirectory,
                                     Integer launchContract) {
        String text = """
                {
                  "manifestVersion": 1,
                  "engineVersion": "1.0.0",
                  "assetFormatVersion": "1.0.0",
                  "projectSchemaVersion": "1.0",
                  "pluginApiVersion": 25,
                  "configuration": "%s",
                  "gitCommit": null,
                  "editorExe": "Editor.exe",
                  "workingDirectory": "%s",
                  "templatesDir": "../../../../assets/templates",
                  "scriptLibDir": "../../../../assets/scripts/lib",
                  "examplesDir": "../../../../assets/scripts/game/examples",
                  "vfxLibraryDir": "../../../../assets/vfx/library"%s
                }
                """.formatted(configuration, workingDirectory,
                launchContract == null ? "" : ",\n  \"launchContract\": " + launchContract);
        try {
            return Files.writeString(Files.createDirectories(dir).resolve("engine.json"), text, StandardCharsets.UTF_8);
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    /**
     * A Development build laid out as in this repository: {@code repo/bin/Editor/Development/x64/Editor.exe} with an
     * {@code engine.json} whose working directory {@code ../../../../VFEngine/editor} exists.
     *
     * @return the editor
     */
    public static Path devEngine(Path repo) {
        Path exeDir = repo.resolve("bin/Editor/Development/x64");
        Path editor = fakeEditor(exeDir);
        writeManifest(exeDir, "Development", "../../../../VFEngine/editor");
        try {
            Files.createDirectories(repo.resolve("VFEngine/editor"));
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
        return editor;
    }

    /**
     * A release folder as {@code tools/package_release.lua} lays it out (VK-1693): the folder is the launcher's app
     * image, {@code VertexForge Launcher.exe} with {@code app/} and {@code runtime/}, beside
     * {@code bin/Editor/Release/x64/Editor.exe} and its {@code engine.json} (working directory {@code "."}).
     *
     * @return the launcher's exe
     */
    public static Path packagedLauncher(Path releaseRoot) {
        try {
            Files.createDirectories(releaseRoot.resolve("app"));
            Files.createDirectories(releaseRoot.resolve("runtime"));
            Path launcher = Files.writeString(releaseRoot.resolve("VertexForge Launcher.exe"), "fake launcher");
            Path exeDir = releaseRoot.resolve("bin/Editor/Release/x64");
            fakeEditor(exeDir);
            writeManifest(exeDir, "Release", ".");
            return launcher;
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    /**
     * An engine's project content laid out as in this repository's {@code assets/} (VK-1689), written under
     * {@code root}: the {@code empty} template ({@link #FAKE_PAYLOAD}, both options), the {@link #FAKE_LIBRARY},
     * one example and one effect. Writing it again over the same root is harmless.
     *
     * @return the content of a registered engine ({@link #FAKE_ENGINE_ID}) whose folders are these
     */
    public static EngineContent engineContent(Path root) {
        writeEngineContent(root);
        Path assets = root.resolve("assets");
        return new EngineContent(Optional.of(FAKE_ENGINE_ID), FAKE_ENGINE_VERSION, assets.resolve("templates"),
                assets.resolve("scripts/lib"), Optional.of(assets.resolve("scripts/game/examples")),
                Optional.of(assets.resolve("vfx/library")), FAKE_ENGINE_VERSION + " Development", "fake", false);
    }

    /** Writes what {@link #engineContent} describes, without the record. */
    public static void writeEngineContent(Path root) {
        try {
            Path assets = root.resolve("assets");
            Path template = Files.createDirectories(assets.resolve("templates").resolve(TEMPLATE));
            Files.writeString(template.resolve("template.json"), """
                    {
                      "formatVersion": 1,
                      "id": "empty",
                      "name": "Empty",
                      "description": "A test template.",
                      "minimumEngineVersion": "1.0.0",
                      "workingDirectory": "assets",
                      "startupScene": "scenes/Main.vfScene",
                      "scriptLibrary": "assets/scripts/lib",
                      "options": [
                        {"id": "examples", "name": "Include examples", "default": false,
                         "target": "assets/scripts/game/examples"},
                        {"id": "vfxLibrary", "name": "Include VFX library", "default": false,
                         "target": "assets/vfx/library"}
                      ]
                    }
                    """, StandardCharsets.UTF_8);
            for (Map.Entry<String, String> file : FAKE_PAYLOAD.entrySet()) {
                write(template.resolve("project").resolve(file.getKey()), file.getValue());
            }
            for (Map.Entry<String, String> file : FAKE_LIBRARY.entrySet()) {
                write(assets.resolve("scripts/lib").resolve(file.getKey()), file.getValue());
            }
            write(assets.resolve("scripts/game/examples/PlayerMovement.mt"), "public class PlayerMovement {\n}\n");
            write(assets.resolve("vfx/library/sparks.vfVFX"), "{}\n");
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    /**
     * A blueprint for a project holding these files (project-relative targets), made without touching the disk: for
     * the checks that only need the path budget and the file count.
     */
    public static ProjectBlueprint blueprint(String... targets) {
        EngineContent engine = new EngineContent(Optional.of(FAKE_ENGINE_ID), FAKE_ENGINE_VERSION,
                Path.of("templates").toAbsolutePath(), Path.of("lib").toAbsolutePath(), Optional.empty(),
                Optional.empty(), "fake", "fake", false);
        try {
            TemplateManifest template = TemplateManifest.parse("""
                    {"formatVersion": 1, "id": "empty", "name": "Empty", "minimumEngineVersion": "1.0.0",
                     "workingDirectory": "assets", "startupScene": "scenes/Main.vfScene",
                     "scriptLibrary": "assets/scripts/lib"}
                    """);
            List<ProjectBlueprint.Entry> entries = new ArrayList<>();
            for (String target : targets) {
                entries.add(new ProjectBlueprint.Entry(Path.of("source").toAbsolutePath(), target,
                        Placeholders.Fill.NONE, null));
            }
            return ProjectBlueprint.of(engine, template, entries);
        } catch (TemplateException e) {
            throw new IllegalStateException(e);
        }
    }

    /** Creating {@code name} in {@code location} from the fake engine's {@code empty} template, with these options. */
    public static CreateRequest request(String name, Path location, EngineContent engine, String... options) {
        return new CreateRequest(name, location, engine, TEMPLATE, Set.of(options));
    }

    private static void write(Path file, String content) throws IOException {
        Files.createDirectories(file.getParent());
        Files.writeString(file, content, StandardCharsets.UTF_8);
    }

    /** A clock that moves one second forward on every read, so consecutive timestamps never tie. */
    public static final class TickingClock extends Clock {
        private Instant next;

        public TickingClock(Instant start) {
            this.next = start;
        }

        @Override
        public ZoneId getZone() {
            return ZoneOffset.UTC;
        }

        @Override
        public Clock withZone(ZoneId zone) {
            throw new UnsupportedOperationException();
        }

        @Override
        public synchronized Instant instant() {
            Instant now = next;
            next = next.plusSeconds(1);
            return now;
        }
    }

    public static final class CapturedLog {
        private final ByteArrayOutputStream console = new ByteArrayOutputStream();
        public final LauncherLog log;

        public CapturedLog(Path file, long maxBytes) {
            this.log = new LauncherLog(file, fixedClock(), maxBytes, LauncherLog.DEFAULT_BACKUPS,
                    new PrintStream(console, true, StandardCharsets.UTF_8));
        }

        public String console() {
            return console.toString(StandardCharsets.UTF_8);
        }
    }
}
