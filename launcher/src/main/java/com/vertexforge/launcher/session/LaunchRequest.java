package com.vertexforge.launcher.session;

import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.model.McpSetting;

import java.nio.file.Path;
import java.util.Objects;

/**
 * What to start (VK-1683).
 *
 * @param engine        the engine whose editor opens the project
 * @param project       the project file
 * @param projectName   the name its log is named after and messages use
 * @param mcp           the project's "Open with MCP"
 * @param consoleWanted the "Show editor console" setting; only an editor that accepts {@code --console} gets it
 */
public record LaunchRequest(EngineOption engine, Path project, String projectName, McpSetting mcp,
                            boolean consoleWanted) {
    public LaunchRequest {
        Objects.requireNonNull(engine, "engine");
        Objects.requireNonNull(project, "project");
        Objects.requireNonNull(projectName, "projectName");
        Objects.requireNonNull(mcp, "mcp");
    }
}
