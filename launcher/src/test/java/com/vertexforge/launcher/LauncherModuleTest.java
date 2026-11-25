package com.vertexforge.launcher;

import com.vertexforge.launcher.util.VersionNumber;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.io.InputStream;
import java.lang.module.ModuleDescriptor;
import java.math.BigInteger;
import java.net.URISyntaxException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;
import java.util.stream.Collectors;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The launcher's module (VK-1692). The unit tests run from the classpath, which ignores module-info, so this reads the
 * compiled descriptor itself. The app image's self-test then proves it on the real runtime.
 */
class LauncherModuleTest {

    /** A {@code requires} change also changes what the image must link. */
    private static final String KEEP_IN_STEP = " (module-info.java's requires are also the image profile's jlink"
            + " --add-modules in pom.xml, and they decide the runtime modules launcher.yml checks: change all three)";

    @Test
    void theModuleRequiresExactlyWhatTheImageLinks() throws Exception {
        ModuleDescriptor module = descriptor();

        assertEquals("com.vertexforge.launcher", module.name());
        Map<String, Set<ModuleDescriptor.Requires.Modifier>> requires = module.requires().stream()
                .filter(r -> !r.modifiers().contains(ModuleDescriptor.Requires.Modifier.MANDATED))
                .collect(Collectors.toMap(ModuleDescriptor.Requires::name, ModuleDescriptor.Requires::modifiers));
        assertEquals(Map.of("javafx.controls", Set.of(), "javafx.fxml", Set.of(), "com.google.gson", Set.of()),
                requires, "requires" + KEEP_IN_STEP);
    }

    @Test
    void theModuleOpensOnlyWhatJavaFxReflectsOn() throws Exception {
        ModuleDescriptor module = descriptor();

        assertFalse(module.isOpen(), "an open module would open every package to every module");
        // Application.launch constructs LauncherApp from javafx.graphics.
        assertEquals(Set.of(Launcher.class.getPackageName() + " to [javafx.graphics]"),
                module.exports().stream()
                        .map(e -> e.source() + " to " + new TreeSet<>(e.targets()) + modifiers(e.modifiers()))
                        .collect(Collectors.toSet()));
        // FXMLLoader injects MainController's private @FXML members. Gson only reads and writes JSON trees, so no
        // package is open to it: binding a launcher class with Gson would need one, and would fail only in the image.
        assertEquals(Set.of("com.vertexforge.launcher.controller to [javafx.fxml]"),
                module.opens().stream()
                        .map(o -> o.source() + " to " + new TreeSet<>(o.targets()) + modifiers(o.modifiers()))
                        .collect(Collectors.toSet()));
        assertTrue(module.uses().isEmpty(), "uses " + module.uses());
        assertTrue(module.provides().isEmpty(), "provides " + module.provides());
    }

    /**
     * The image profile reads {@code ../VERSION} as {@code tools/engine_version.lua} does and also refuses a part above
     * 65535, which the exe's version resource cannot hold; this lets a plain {@code mvnw verify} catch such a VERSION.
     */
    @Test
    void theRepoVersionFitsTheExeVersionResource() throws IOException {
        Path file = Path.of(System.getProperty("vertexforge.repoRoot", "..")).toAbsolutePath().normalize()
                .resolve("VERSION");
        String text = Files.readString(file, StandardCharsets.UTF_8);
        String withoutBom = text.startsWith("﻿") ? text.substring(1) : text;
        String version = withoutBom.replaceAll("^\\s+|\\s+$", "");

        VersionNumber parsed = VersionNumber.parse(version).orElseThrow(() -> new AssertionError(
                file + " holds '" + version + "', which is not MAJOR.MINOR.PATCH"));
        BigInteger limit = BigInteger.valueOf(65535);
        for (BigInteger part : List.of(parsed.major(), parsed.minor(), parsed.patch())) {
            assertTrue(part.compareTo(limit) <= 0, file + " holds " + version
                    + ": the launcher exe's version resource holds at most 65535 per part");
        }
    }

    /** {@code target/classes/module-info.class}, from the folder the launcher's classes come from. */
    private static ModuleDescriptor descriptor() throws IOException, URISyntaxException {
        Path classes = Path.of(Launcher.class.getProtectionDomain().getCodeSource().getLocation().toURI());
        Path file = classes.resolve("module-info.class");
        assertTrue(Files.isRegularFile(file), file + " is missing");
        try (InputStream in = Files.newInputStream(file)) {
            return ModuleDescriptor.read(in);
        }
    }

    private static String modifiers(Set<?> modifiers) {
        return modifiers.isEmpty() ? "" : " " + new TreeSet<>(modifiers.stream().map(Object::toString).toList());
    }
}
