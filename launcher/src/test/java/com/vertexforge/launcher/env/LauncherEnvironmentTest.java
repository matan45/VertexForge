package com.vertexforge.launcher.env;

import org.junit.jupiter.api.Test;

import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class LauncherEnvironmentTest {

    @Test
    void systemUsesTheUserHomeSettingsFolderAndTheWorkingDirectory() {
        LauncherEnvironment environment = LauncherEnvironment.system();

        assertEquals(Path.of(System.getProperty("user.home"), ".vertexforge"), environment.configDir());
        assertEquals(Path.of(System.getProperty("user.dir")), environment.workingDir());
        assertEquals(System.getenv("PATH"), environment.env().apply("PATH"));
    }

    @Test
    void testsRunWithASandboxedUserHome() {
        // pom.xml points user.home into target/ for the test JVM, so nothing here can touch ~/.vertexforge.
        Path home = Path.of(System.getProperty("user.home")).normalize();
        assertTrue(home.endsWith(Path.of("target", "test-home")), "user.home is " + home);
    }
}
