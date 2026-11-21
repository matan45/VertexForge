package com.vertexforge.launcher.project;

import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

/**
 * What VK-1695's Duplicate and Clean Caches assume about the engine and mType must follow their source: where the
 * editor reads the scripts project and writes the compiled library, how mType's parser reads the project's name, and
 * which folders engine.json names. A change there fails here until the launcher follows. The sources are read from
 * this checkout; a partial checkout without them (the launcher CI's) skips these checks.
 */
class ProjectActionsParityGuardTest {

    private static String source(String repoRelative) throws IOException {
        Path file = Path.of(System.getProperty("vertexforge.repoRoot", "..")).resolve(repoRelative)
                .toAbsolutePath().normalize();
        assumeTrue(Files.isRegularFile(file), file + " is not in this checkout");
        return Files.readString(file, StandardCharsets.UTF_8);
    }

    private static void assertContains(String source, String expected, String what) {
        assertTrue(source.contains(expected), what + ": the source no longer says " + expected);
    }

    /** ProjectContent.SCRIPTS_PROJECT, which Duplicate renames. */
    @Test
    void theEditorReadsTheScriptsProjectWhereDuplicateLooks() throws IOException {
        assertContains(source("VFEngine/services/impl/scripting/ScriptingServiceImpl.cpp"),
                "(workingDir / \"scripts\" / \"scripts.mtproj\")", "the scripts project's path");
        assertTrue(ProjectContent.SCRIPTS_PROJECT.equals("scripts/scripts.mtproj"));
    }

    /** ProjectContent.COMPILED_SCRIPTS, the folder engine.json lists for Clean Caches. */
    @Test
    void theEditorsCompiledLibraryIsInTheFolderEngineJsonLists() throws IOException {
        assertContains(source("VFEngine/core/adapters/scripting/ScriptingAdapter.cpp"),
                "(manifestDir / \"compiled\" / \"scripts.mtcLib\")", "the compiled library's path");
        assertContains(source("tools/engine_version.lua"), "\"" + ProjectContent.COMPILED_SCRIPTS + "\"",
                "engine.json's projectCacheDirs");
    }

    /** The manifest tools/engine_version.lua writes is read as the launcher expects (a dev build of VK-1695 or later). */
    @Test
    void theDevManifestListsTheCompiledScriptsForCleanCaches() throws Exception {
        Path manifest = Path.of(System.getProperty("vertexforge.repoRoot", "..")).resolve(
                "bin/Editor/Development/x64/engine.json").toAbsolutePath().normalize();
        assumeTrue(Files.isRegularFile(manifest), "no Development build in this checkout");
        com.vertexforge.launcher.engine.EngineManifest parsed =
                com.vertexforge.launcher.engine.EngineManifest.read(manifest);
        assumeTrue(parsed.projectCacheDirs() != null, "the Development build is older than VK-1695");

        assertTrue(parsed.projectCacheDirs().equals(java.util.List.of(ProjectContent.COMPILED_SCRIPTS)),
                manifest + " lists " + parsed.projectCacheDirs());
    }

    /** ScriptsProjectEdit reads the name as mType's own parser does. */
    @Test
    void mTypeReadsTheProjectsNameAsScriptsProjectEditDoes() throws IOException {
        String parser = source("dependencies/mType/mType/project/XmlParser.cpp");
        assertContains(parser, "if (pos >= xml.size() || xml[pos] != '\"')", "only a double-quoted value counts");
        assertContains(parser, "attrs[attrName] = attrValue;", "a repeated name: the last one wins");
        assertContains(parser, "if (xml.substr(pos, 5) == \"<?xml\")", "the optional prolog");
        String config = source("dependencies/mType/mType/project/ProjectConfigParser.cpp");
        assertContains(config, "if (root.tagName != \"Project\")", "the root element");
        assertContains(config, "root.attributes.find(\"Name\")", "the name's attribute");
    }
}
