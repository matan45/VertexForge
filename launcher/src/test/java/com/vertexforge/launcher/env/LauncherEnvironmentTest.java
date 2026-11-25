package com.vertexforge.launcher.env;

import org.junit.jupiter.api.Test;

import java.nio.charset.Charset;
import java.nio.file.Path;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class LauncherEnvironmentTest {

    @Test
    void systemUsesTheUserHomeSettingsFolderAndNoLauncherExeOutsideThePackagedImage() {
        LauncherEnvironment environment = LauncherEnvironment.system();

        assertEquals(Path.of(System.getProperty("user.home"), ".vertexforge"), environment.configDir());
        // Only jpackage's native launcher sets jpackage.app-path; surefire never does.
        assertEquals(Optional.empty(), environment.launcherExecutable());
        assertEquals(System.getenv("PATH"), environment.env().apply("PATH"));
    }

    @Test
    void theLauncherExeMustBeAnAbsolutePath() {
        Path exe = Path.of(System.getProperty("java.io.tmpdir")).toAbsolutePath()
                .resolve("VertexForge Launcher").resolve("VertexForge Launcher.exe");

        assertEquals(Optional.of(exe), LauncherEnvironment.launcherExecutable(exe.toString()));
        assertEquals(Optional.of(exe), LauncherEnvironment.launcherExecutable(
                exe.getParent().resolve("app").resolve("..").resolve(exe.getFileName()).toString()));
        assertEquals(Optional.empty(), LauncherEnvironment.launcherExecutable(null));
        assertEquals(Optional.empty(), LauncherEnvironment.launcherExecutable(" "));
        assertEquals(Optional.empty(), LauncherEnvironment.launcherExecutable("VertexForge Launcher.exe"));
        assertEquals(Optional.empty(), LauncherEnvironment.launcherExecutable("bad\u0000path"));
    }

    @Test
    void theNativeCharsetComesFromNativeEncodingWithTheDefaultAsFallback() {
        assertEquals(Charset.forName("windows-1252"), LauncherEnvironment.nativeCharset("Cp1252"));
        assertEquals(Charset.defaultCharset(), LauncherEnvironment.nativeCharset(null));
        assertEquals(Charset.defaultCharset(), LauncherEnvironment.nativeCharset(" "));
        assertEquals(Charset.defaultCharset(), LauncherEnvironment.nativeCharset("no-such-charset"));
        assertEquals(LauncherEnvironment.nativeCharset(System.getProperty("native.encoding")),
                LauncherEnvironment.system().nativeCharset());
    }

    @Test
    void testsRunWithASandboxedUserHome() {
        // pom.xml points user.home into target/ for the test JVM, so nothing here can touch ~/.vertexforge.
        Path home = Path.of(System.getProperty("user.home")).normalize();
        assertTrue(home.endsWith(Path.of("target", "test-home")), "user.home is " + home);
    }
}
