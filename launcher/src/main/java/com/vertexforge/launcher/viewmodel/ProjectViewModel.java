package com.vertexforge.launcher.viewmodel;

import com.vertexforge.launcher.model.ProjectInfo;
import com.vertexforge.launcher.util.ProjectValidator;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.beans.property.SimpleStringProperty;
import javafx.beans.property.StringProperty;

import java.nio.file.Path;

public class ProjectViewModel {

    private final ProjectInfo projectInfo;
    private final StringProperty name;
    private final StringProperty path;
    private final BooleanProperty valid;


    public ProjectViewModel(ProjectInfo projectInfo) {
        this.projectInfo = projectInfo;
        this.name = new SimpleStringProperty(projectInfo.name());
        this.path = new SimpleStringProperty(projectInfo.path().toString());
        this.valid = new SimpleBooleanProperty(ProjectValidator.isValid(projectInfo.path()));
    }


    public String getName() {
        return name.get();
    }

    public String getPath() {
        return path.get();
    }

    public boolean isValid() {
        return valid.get();
    }

    public Path getProjectPath() {
        return projectInfo.path();
    }

    public void refreshValidity() {
        valid.set(ProjectValidator.isValid(projectInfo.path()));
    }
}
