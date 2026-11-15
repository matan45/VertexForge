package com.vertexforge.launcher.viewmodel;

import com.vertexforge.launcher.engine.EngineLabels;
import com.vertexforge.launcher.model.ProjectStatus;
import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.session.EditorSession;
import javafx.beans.binding.Bindings;
import javafx.beans.binding.BooleanBinding;
import javafx.beans.binding.ObjectBinding;
import javafx.beans.property.ObjectProperty;
import javafx.beans.property.ReadOnlyObjectProperty;
import javafx.beans.property.ReadOnlyObjectWrapper;
import javafx.beans.property.ReadOnlyStringProperty;
import javafx.beans.property.ReadOnlyStringWrapper;
import javafx.beans.property.SimpleObjectProperty;

import java.nio.file.Path;
import java.time.Instant;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.Objects;
import java.util.UUID;

/**
 * One row of the projects table (VK-1678). It never touches the file system: its status is set from the background
 * checks. The FX thread owns it; the properties are plain {@code javafx.beans} ones, so it also works in unit tests
 * without the JavaFX toolkit.
 * <p>
 * A row is reused while its entry keeps its id and path, so a status (or a check still in flight) survives a reload.
 */
public final class ProjectViewModel {

    /** Shown for an entry whose stored time is missing or invalid (only a hand-edited file has one). */
    public static final String UNKNOWN_DATE = "Unknown";
    /** Shown in the Path column for an entry with no stored path. */
    public static final String NO_PATH = "(no path)";

    private static final DateTimeFormatter LAST_OPENED_FORMAT = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm");

    private RecentEntry entry;
    private final ReadOnlyStringWrapper name;
    private final ReadOnlyStringWrapper pathText;
    private final ReadOnlyObjectWrapper<Instant> lastOpened;
    private final ReadOnlyStringWrapper engineText;
    private final ObjectProperty<ProjectStatus> status = new SimpleObjectProperty<>(ProjectStatus.CHECKING);
    /** The phase of this launcher's editor on the project (VK-1683); {@code null} when none runs. */
    private final ObjectProperty<EditorSession.Phase> sessionPhase = new SimpleObjectProperty<>(null);
    private final ObjectBinding<StatusDisplay> statusDisplay;
    private final BooleanBinding openable;
    private long probeToken;

    public ProjectViewModel(RecentEntry entry) {
        this.entry = Objects.requireNonNull(entry, "entry");
        this.name = new ReadOnlyStringWrapper(entry.displayName());
        this.pathText = new ReadOnlyStringWrapper(pathTextOf(entry));
        this.lastOpened = new ReadOnlyObjectWrapper<>(entry.lastOpened());
        // Until the engines have been checked (VK-1681); the window then sets the real text.
        this.engineText = new ReadOnlyStringWrapper(EngineLabels.forProject(entry.engineId(), null));
        // The path cannot change for this row (see matches), so only the status needs observing.
        this.openable = Bindings.createBooleanBinding(
                () -> status.get().isOk() && this.entry.path() != null, status);
        this.statusDisplay = Bindings.createObjectBinding(
                () -> new StatusDisplay(status.get(), sessionPhase.get()), status, sessionPhase);
    }

    /** {@code yyyy-MM-dd HH:mm} in {@code zone}, or {@link #UNKNOWN_DATE}. */
    public static String formatLastOpened(Instant instant, ZoneId zone) {
        if (instant == null) {
            return UNKNOWN_DATE;
        }
        return LAST_OPENED_FORMAT.format(instant.atZone(zone));
    }

    /** Whether this row can show {@code other}: the same id and the same path. */
    public boolean matches(RecentEntry other) {
        return entry.id().equals(other.id()) && Objects.equals(entry.path(), other.path());
    }

    /** Shows a newer version of the same entry (a new name or time); the status is kept. */
    public void update(RecentEntry newer) {
        if (!matches(newer)) {
            throw new IllegalArgumentException("Not the same entry: " + newer.id());
        }
        entry = newer;
        name.set(newer.displayName());
        pathText.set(pathTextOf(newer));
        lastOpened.set(newer.lastOpened());
    }

    public RecentEntry getEntry() {
        return entry;
    }

    public UUID getId() {
        return entry.id();
    }

    /** The project file, or {@code null} when the stored path is not a valid absolute path. */
    public Path getProjectPath() {
        return entry.path();
    }

    public String getName() {
        return name.get();
    }

    public ReadOnlyStringProperty nameProperty() {
        return name.getReadOnlyProperty();
    }

    public String getPath() {
        return pathText.get();
    }

    public ReadOnlyStringProperty pathTextProperty() {
        return pathText.getReadOnlyProperty();
    }

    public Instant getLastOpened() {
        return lastOpened.get();
    }

    public ReadOnlyObjectProperty<Instant> lastOpenedProperty() {
        return lastOpened.getReadOnlyProperty();
    }

    /** The Engine column's text (see {@link EngineLabels}); never blank. */
    public String getEngineText() {
        return engineText.get();
    }

    public void setEngineText(String text) {
        Objects.requireNonNull(text, "text");
        engineText.set(text);
    }

    public ReadOnlyStringProperty engineTextProperty() {
        return engineText.getReadOnlyProperty();
    }

    public ProjectStatus getStatus() {
        return status.get();
    }

    public void setStatus(ProjectStatus newStatus) {
        status.set(Objects.requireNonNull(newStatus, "newStatus"));
    }

    public ObjectProperty<ProjectStatus> statusProperty() {
        return status;
    }

    /** The phase of this launcher's editor on the project; {@code null} when none runs (VK-1683). */
    public EditorSession.Phase getSessionPhase() {
        return sessionPhase.get();
    }

    public void setSessionPhase(EditorSession.Phase phase) {
        sessionPhase.set(phase == EditorSession.Phase.EXITED ? null : phase);
    }

    public ObjectProperty<EditorSession.Phase> sessionPhaseProperty() {
        return sessionPhase;
    }

    /** The Status column's value: the file's status, or the running editor's phase. */
    public StatusDisplay getStatusDisplay() {
        return statusDisplay.get();
    }

    public ObjectBinding<StatusDisplay> statusDisplayProperty() {
        return statusDisplay;
    }

    public boolean isOpenable() {
        return openable.get();
    }

    public BooleanBinding openableProperty() {
        return openable;
    }

    /** Starts a new status check of this row; only the answer to the latest check is applied. */
    public long startProbe() {
        return ++probeToken;
    }

    public boolean isCurrentProbe(long token) {
        return token == probeToken;
    }

    private static String pathTextOf(RecentEntry entry) {
        return entry.rawPath() != null && !entry.rawPath().isBlank() ? entry.rawPath() : NO_PATH;
    }
}
