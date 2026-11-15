package com.vertexforge.launcher.session;

import com.vertexforge.launcher.model.McpSetting;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;

/**
 * The editors this launcher started and that still run, one per project (VK-1683). Opening a project that is in here
 * starts nothing. Owned by the FX thread; phases are the last ones the sessions reported there.
 * <p>
 * An editor started elsewhere is not in here: the editor itself refuses a project another editor holds (exit code 5,
 * VK-1682), which the launcher then reports.
 */
public final class EditorSessions {

    /** One running editor, as the window knows it. */
    public static final class Live {
        private final EditorSession session;
        private final Path project;
        private final McpSetting mcp;
        private EditorSession.Phase phase = EditorSession.Phase.STARTING;

        private Live(EditorSession session, Path project, McpSetting mcp) {
            this.session = session;
            this.project = project;
            this.mcp = mcp;
        }

        public EditorSession session() {
            return session;
        }

        /** The project file, normalized ({@link #key}). */
        public Path project() {
            return project;
        }

        public String projectName() {
            return session.start().projectName();
        }

        /** The MCP setting it was started with. */
        public McpSetting mcp() {
            return mcp;
        }

        public EditorSession.Phase phase() {
            return phase;
        }
    }

    private final Map<Path, Live> byProject = new LinkedHashMap<>();

    /** The registry's form of a project path: absolute and normalized ({@code WindowsPath} compares without case). */
    public static Path key(Path project) {
        return project.toAbsolutePath().normalize();
    }

    /**
     * Adds a session that has just started.
     *
     * @throws IllegalStateException when its project already has a live session
     */
    public Live add(EditorSession session, McpSetting mcp) {
        Objects.requireNonNull(session, "session");
        Objects.requireNonNull(mcp, "mcp");
        Path project = key(session.start().project());
        if (byProject.containsKey(project)) {
            throw new IllegalStateException("A session for " + project + " is already running");
        }
        Live live = new Live(session, project, mcp);
        byProject.put(project, live);
        return live;
    }

    public Optional<Live> find(Path project) {
        return project == null ? Optional.empty() : Optional.ofNullable(byProject.get(key(project)));
    }

    public Optional<Live> find(EditorSession session) {
        for (Live live : byProject.values()) {
            if (live.session == session) {
                return Optional.of(live);
            }
        }
        return Optional.empty();
    }

    /** Records a phase the session reported; ignored for a session that is no longer listed. */
    public void setPhase(EditorSession session, EditorSession.Phase phase) {
        find(session).ifPresent(live -> live.phase = phase);
    }

    /** Removes an ended session; does nothing when it is not listed. */
    public void remove(EditorSession session) {
        find(session).ifPresent(live -> byProject.remove(live.project));
    }

    public int liveCount() {
        return byProject.size();
    }

    /** Whether any session has not reported Ready yet, so it may still fail to open its project. */
    public boolean anyBeforeReady() {
        return byProject.values().stream().anyMatch(live -> live.phase.beforeReady());
    }

    /** A live session whose editor was started with its MCP server on {@code port}. */
    public Optional<Live> usingMcpPort(int port) {
        return byProject.values().stream()
                .filter(live -> live.mcp.enabled() && live.mcp.port() == port)
                .findFirst();
    }

    public List<Live> all() {
        return new ArrayList<>(byProject.values());
    }
}
