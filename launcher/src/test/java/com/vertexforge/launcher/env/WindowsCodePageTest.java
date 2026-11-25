package com.vertexforge.launcher.env;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;

import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.util.Optional;
import java.util.OptionalInt;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The system's ANSI code page, which the packaged launcher (running in UTF-8) reads from the registry (VK-1693). */
class WindowsCodePageTest {

    /** The test JVM (java.exe) has no UTF-8 manifest, so its native.encoding IS the system code page. */
    @Test
    @EnabledOnOs(OS.WINDOWS)
    void theRegistryNamesTheCodePageAJvmWithoutTheManifestRunsIn() {
        OptionalInt codePage = WindowsCodePage.systemAnsiCodePage();

        assertTrue(codePage.isPresent(), "the registry value could not be read");
        assertEquals(Optional.of(Charset.forName(System.getProperty("native.encoding"))),
                WindowsCodePage.charsetFor(codePage.getAsInt()), "code page " + codePage.getAsInt());
        assertEquals(Charset.forName(System.getProperty("native.encoding")), LauncherEnvironment.systemAnsiCharset());
    }

    @Test
    void codePagesAreNamedAsTheJdkNamesThem() {
        assertEquals(Optional.of(StandardCharsets.UTF_8), WindowsCodePage.charsetFor(65001));
        assertEquals(Optional.of(Charset.forName("windows-1252")), WindowsCodePage.charsetFor(1252));
        assertEquals(Optional.of(Charset.forName("windows-1255")), WindowsCodePage.charsetFor(1255));
        assertEquals(Optional.of(Charset.forName("windows-31j")), WindowsCodePage.charsetFor(932));
        assertEquals(Optional.of(Charset.forName("GBK")), WindowsCodePage.charsetFor(936));
        assertEquals(Optional.of(Charset.forName("GB18030")), WindowsCodePage.charsetFor(54936));
        assertEquals(Optional.empty(), WindowsCodePage.charsetFor(1));
    }

    @Test
    void onlyAPositiveNumberIsACodePage() {
        assertEquals(OptionalInt.of(1252), WindowsCodePage.parse("1252"));
        assertEquals(OptionalInt.of(65001), WindowsCodePage.parse(" 65001 "));
        assertEquals(OptionalInt.empty(), WindowsCodePage.parse(""));
        assertEquals(OptionalInt.empty(), WindowsCodePage.parse(null));
        assertEquals(OptionalInt.empty(), WindowsCodePage.parse("0"));
        assertEquals(OptionalInt.empty(), WindowsCodePage.parse("-1"));
        assertEquals(OptionalInt.empty(), WindowsCodePage.parse("12a"));
        assertEquals(OptionalInt.empty(), WindowsCodePage.parse("99999999999"));
    }
}
