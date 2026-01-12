package com.vertexforge.launcher.viewmodel;

import com.vertexforge.launcher.model.ProjectInfo;
import com.vertexforge.launcher.util.ProjectValidator;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.beans.property.SimpleStringProperty;
import javafx.beans.property.StringProperty;

import java.nio.file.Path;
import java.time.Instant;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;

/**
 * ViewModel adapter for displaying ProjectInfo in TableView.
 * <p>
 * Bridges the immutable {@link ProjectInfo} record with JavaFX observable
 * properties for data binding in TableView cells.
 * </p>
 */
public class ProjectViewModel {

    /**
     * Date format for displaying last opened timestamp.
     * Example: "Jan 12, 2026 14:30"
     */
    private static final DateTimeFormatter DATE_FORMATTER =
            DateTimeFormatter.ofPattern("MMM dd, yyyy HH:mm")
                    .withZone(ZoneId.systemDefault());

    private final ProjectInfo projectInfo;
    private final StringProperty name;
    private final StringProperty path;
    private final StringProperty lastOpened;
    private final BooleanProperty valid;

    /**
     * Creates a new ViewModel from a ProjectInfo.
     *
     * @param projectInfo the project info to wrap
     */
    public ProjectViewModel(ProjectInfo projectInfo) {
        this.projectInfo = projectInfo;
        this.name = new SimpleStringProperty(projectInfo.name());
        this.path = new SimpleStringProperty(projectInfo.path().toString());
        this.lastOpened = new SimpleStringProperty(formatDate(projectInfo.lastOpened()));
        this.valid = new SimpleBooleanProperty(ProjectValidator.isValid(projectInfo.path()));
    }

    /**
     * Property for project name (for TableView cell binding).
     *
     * @return the name property
     */
    public StringProperty nameProperty() {
        return name;
    }

    /**
     * Property for project path (for TableView cell binding).
     *
     * @return the path property
     */
    public StringProperty pathProperty() {
        return path;
    }

    /**
     * Property for last opened date (for TableView cell binding).
     *
     * @return the lastOpened property
     */
    public StringProperty lastOpenedProperty() {
        return lastOpened;
    }

    /**
     * Property for validity state (for row styling).
     *
     * @return the valid property
     */
    public BooleanProperty validProperty() {
        return valid;
    }

    /**
     * Gets the project name.
     *
     * @return the name
     */
    public String getName() {
        return name.get();
    }

    /**
     * Gets the project path as string.
     *
     * @return the path string
     */
    public String getPath() {
        return path.get();
    }

    /**
     * Gets the formatted last opened date.
     *
     * @return the formatted date string
     */
    public String getLastOpened() {
        return lastOpened.get();
    }

    /**
     * Checks if the project is valid (file exists and accessible).
     *
     * @return true if valid
     */
    public boolean isValid() {
        return valid.get();
    }

    /**
     * Gets the underlying ProjectInfo model.
     *
     * @return the project info
     */
    public ProjectInfo getProjectInfo() {
        return projectInfo;
    }

    /**
     * Gets the project path as a Path object.
     *
     * @return the path
     */
    public Path getProjectPath() {
        return projectInfo.path();
    }

    /**
     * Refreshes the validity status by re-checking the file system.
     * <p>
     * Useful for rescanning after external changes.
     * </p>
     */
    public void refreshValidity() {
        valid.set(ProjectValidator.isValid(projectInfo.path()));
    }

    /**
     * Formats an Instant to a human-readable date string.
     *
     * @param instant the instant to format
     * @return the formatted string
     */
    private static String formatDate(Instant instant) {
        return DATE_FORMATTER.format(instant);
    }
}
