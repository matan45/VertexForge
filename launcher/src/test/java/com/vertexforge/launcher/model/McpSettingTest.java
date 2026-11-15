package com.vertexforge.launcher.model;

import com.google.gson.JsonParser;
import com.vertexforge.launcher.TestSupport;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.file.Path;
import java.util.List;
import java.util.OptionalInt;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class McpSettingTest {

    @TempDir
    Path tempDir;

    @Test
    void portsFollowTheEditorsRange() {
        assertEquals(OptionalInt.of(7878), McpSetting.parsePort("7878"));
        assertEquals(OptionalInt.of(1024), McpSetting.parsePort("1024"));
        assertEquals(OptionalInt.of(65535), McpSetting.parsePort("65535"));
        for (String bad : new String[]{"", "1023", "65536", "+7878", " 7878", "7878 ", "0x10", "-1", "99999999", "a"}) {
            assertEquals(OptionalInt.empty(), McpSetting.parsePort(bad), bad);
        }
        assertThrows(IllegalArgumentException.class, () -> new McpSetting(true, 80));
    }

    @Test
    void theStoredMemberIsReadForgivingly() {
        assertEquals(new McpSetting(true, 7979),
                McpSetting.fromJson(JsonParser.parseString("{\"enabled\": true, \"port\": 7979}")));
        assertEquals(McpSetting.DEFAULT, McpSetting.fromJson(null));
        assertEquals(McpSetting.DEFAULT, McpSetting.fromJson(JsonParser.parseString("\"on\"")));
        assertEquals(new McpSetting(true, McpSetting.DEFAULT_PORT),
                McpSetting.fromJson(JsonParser.parseString("{\"enabled\": true, \"port\": 80}")));
        assertEquals(new McpSetting(false, 7979),
                McpSetting.fromJson(JsonParser.parseString("{\"enabled\": \"yes\", \"port\": 7979}")));
    }

    @Test
    void anEntryStoresItAsItsOwnMemberAndKeepsTheRest() {
        RecentEntry entry = RecentEntry.create(UUID.randomUUID(), tempDir.resolve("Game.vfproj"), "Game",
                TestSupport.T0);
        assertEquals(McpSetting.DEFAULT, entry.mcp(), "never set");

        RecentEntry changed = entry.withMcp(new McpSetting(true, 7979));

        assertEquals(new McpSetting(true, 7979), changed.mcp());
        assertEquals(JsonParser.parseString("{\"enabled\": true, \"port\": 7979}"), changed.raw().get(RecentEntry.MCP));
        assertEquals(entry.name(), changed.name());
        assertEquals(entry.path(), changed.path());
        assertSame(changed, changed.withMcp(new McpSetting(true, 7979)), "no change, same entry");
        assertTrue(changed.withPinned(true).raw().has(RecentEntry.MCP), "other changes keep it");
    }

    @Test
    void theListSetsItOnTheRightEntry() {
        UUID a = UUID.randomUUID();
        UUID b = UUID.randomUUID();
        RecentProjects projects = new RecentProjects(List.of(
                RecentEntry.create(a, tempDir.resolve("A.vfproj"), "A", TestSupport.T0),
                RecentEntry.create(b, tempDir.resolve("B.vfproj"), "B", TestSupport.T0)));

        RecentProjects changed = projects.setMcp(b, new McpSetting(true, 7000));

        assertEquals(McpSetting.DEFAULT, changed.entries().get(0).mcp());
        assertEquals(new McpSetting(true, 7000), changed.entries().get(1).mcp());
        assertSame(changed, changed.setMcp(b, new McpSetting(true, 7000)));
        assertSame(projects, projects.setMcp(UUID.randomUUID(), new McpSetting(true, 7000)));
    }
}
