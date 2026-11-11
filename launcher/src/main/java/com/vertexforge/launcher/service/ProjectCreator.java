package com.vertexforge.launcher.service;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.vertexforge.launcher.model.ProjectCreationResult;
import com.vertexforge.launcher.util.LauncherLog;
import com.vertexforge.launcher.util.ZipExtractor;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Clock;
import java.time.Instant;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Objects;


public class ProjectCreator {

    // Packed from assets/scripts/lib at build time (pom.xml, pack-script-lib).
    public static final String LIBRARY_ZIP_RESOURCE = "/templates/script-library.zip";
    public static final String SCENE_TEMPLATE_RESOURCE = "/templates/Main.vfScene";
    private static final String ENGINE_VERSION = "VertexForge 1.0.0";

    private final Gson gson;
    private final Clock clock;
    private final LauncherLog log;

    public ProjectCreator(Clock clock, LauncherLog log) {
        this.clock = Objects.requireNonNull(clock, "clock");
        this.log = Objects.requireNonNull(log, "log");
        this.gson = new GsonBuilder()
                .setPrettyPrinting()
                .create();
    }

    /**
     * Creates a new project with the specified name at the given location.
     * <p>
     * Creates the following structure:
     * <pre>
     * {projectName}/
     * ├── {projectName}.vfproj
     * ├── scenes/
     * │   └── Main.vfScene
     * ├── assets/
     * └── scripts/
     *     ├── scripts.mtproj
     *     ├── game/
     *     │   └── Main.mt
     *     ├── compiled/
     *     └── lib/
     * </pre>
     * </p>
     *
     * @param projectName     the name of the project
     * @param parentDirectory the directory where the project folder will be created
     * @return the result of the creation operation
     */
    public ProjectCreationResult createProject(String projectName, Path parentDirectory) {
        Path projectDir = parentDirectory.resolve(projectName);
        Path projectFile = projectDir.resolve(projectName + ".vfproj");

        try {
            // Create directory structure
            createDirectoryStructure(projectDir);

            // Generate configuration files
            createProjectFile(projectName, projectFile);
            createStartupScene(projectDir.resolve("scenes"));
            createScriptProject(projectName, projectDir.resolve("scripts"));
            createMainScript(projectDir.resolve("scripts").resolve("game"));

            // Extract mType library
            extractLibrary(projectDir.resolve("scripts").resolve("lib"));

            return ProjectCreationResult.success(projectFile);

        } catch (IOException e) {
            // Attempt cleanup on failure
            cleanupOnFailure(projectDir);
            String errorMessage = e.getMessage() != null
                    ? e.getMessage()
                    : "Unknown error: " + e.getClass().getSimpleName();
            return ProjectCreationResult.failure(errorMessage);
        }
    }

    private void createDirectoryStructure(Path projectDir) throws IOException {
        Files.createDirectories(projectDir);
        Files.createDirectories(projectDir.resolve("scenes"));
        Files.createDirectories(projectDir.resolve("assets"));
        Files.createDirectories(projectDir.resolve("scripts"));
        Files.createDirectories(projectDir.resolve("scripts").resolve("game"));
        Files.createDirectories(projectDir.resolve("scripts").resolve("compiled"));
        Files.createDirectories(projectDir.resolve("scripts").resolve("lib"));
    }


    private void createProjectFile(String projectName, Path projectFile) throws IOException {
        // Use LinkedHashMap to preserve field order in JSON
        Map<String, Object> projectConfig = new LinkedHashMap<>();
        projectConfig.put("schemaVersion", "1.0");
        projectConfig.put("projectName", projectName);
        projectConfig.put("version", "0.1.0");
        projectConfig.put("workingDirectory", ".");
        projectConfig.put("startupScene", "scenes/Main.vfScene");
        projectConfig.put("exeIconPath", "");
        projectConfig.put("engineVersion", ENGINE_VERSION);
        projectConfig.put("lastModified", formatTimestamp(clock.instant()));

        String json = gson.toJson(projectConfig);
        Files.writeString(projectFile, json);
    }


    private void createStartupScene(Path scenesDir) throws IOException {
        Path sceneFile = scenesDir.resolve("Main.vfScene");

        try (InputStream templateStream = getClass().getResourceAsStream(SCENE_TEMPLATE_RESOURCE)) {
            if (templateStream == null) {
                throw new IOException("Scene template not found: " + SCENE_TEMPLATE_RESOURCE);
            }
            Files.copy(templateStream, sceneFile);
        }
    }

    private void createScriptProject(String projectName, Path scriptsDir) throws IOException {
        Path mtprojFile = scriptsDir.resolve("scripts.mtproj");

        String xml = """
                <?xml version="1.0" encoding="UTF-8"?>
                <Project Name="%s" Version="1.0.0">
                  <Source>
                    <Include>game/**/*.mt</Include>
                  </Source>
                  <Output Directory="compiled" />
                  <Imports>
                    <SearchPath>lib</SearchPath>
                  </Imports>
                </Project>
                """.formatted(projectName);

        Files.writeString(mtprojFile, xml);
    }


    private void createMainScript(Path gameDir) throws IOException {
        Path mainScript = gameDir.resolve("Main.mt");

        String content = """
                import * from "engine/Log.mt";
                import * from "engine/Entity.mt";

                @Script
                public class Main {
                    private int selfId;
               
                    constructor() {
                        // initialization code
                    }
              
                    public function onStart(): void {
                        this.selfId = Entity::self();
                        Log::info("Hello from Main script!");
                    }

                    public function onUpdate(float deltaTime): void {
                        // Called every frame
                    }

                    public function onFixedUpdate(float fixedDeltaTime): void {
                        // Called at fixed time intervals (after each physics step)
                    }

                    public function onLateUpdate(float deltaTime): void {
                        // Called after all onUpdate calls (e.g. camera follow)
                    }

                    public function onDestroy(): void {
                        // Called when script is destroyed
                    }
                }
               \s""";

        Files.writeString(mainScript, content);
    }


    private void extractLibrary(Path libDir) throws IOException {
        try (InputStream resourceStream = getClass().getResourceAsStream(LIBRARY_ZIP_RESOURCE)) {
            if (resourceStream == null) {
                throw new IOException("Script library " + LIBRARY_ZIP_RESOURCE + " is missing from the launcher. "
                        + "It is packed from assets/scripts/lib at build time: rebuild the launcher with "
                        + "'mvnw package' in launcher/ (see launcher/build.txt).");
            }

            ZipExtractor.extract(resourceStream, libDir);
        }
    }


    private void cleanupOnFailure(Path projectDir) {
        try {
            if (Files.exists(projectDir)) {
                try (var paths = Files.walk(projectDir)) {
                    paths.sorted(Comparator.reverseOrder())
                            .forEach(path -> {
                                try {
                                    Files.delete(path);
                                } catch (IOException e) {
                                    log.warn("Warning: Could not delete " + path + ": " + e.getMessage());
                                }
                            });
                }
            }
        } catch (IOException e) {
            log.warn("Warning: Failed to cleanup partial project: " + e.getMessage());
        }
    }

    private String formatTimestamp(Instant instant) {
        return DateTimeFormatter.ISO_INSTANT.format(instant.atOffset(ZoneOffset.UTC));
    }
}
