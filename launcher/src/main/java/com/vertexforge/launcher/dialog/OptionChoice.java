package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.template.TemplateManifest;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.SimpleBooleanProperty;

import java.util.Objects;
import java.util.Optional;

/**
 * A template option as the Create Project dialog shows it (VK-1689): a checkbox, disabled with its reason when the
 * engine cannot provide what the option copies.
 */
public final class OptionChoice {

    private final TemplateManifest.Option option;
    private final String unavailableReason;
    private final BooleanProperty selected;

    /** @param unavailableReason why it cannot be added, or {@code null} when it can */
    public OptionChoice(TemplateManifest.Option option, String unavailableReason) {
        this.option = Objects.requireNonNull(option, "option");
        this.unavailableReason = unavailableReason;
        this.selected = new SimpleBooleanProperty(this, "selected", option.defaultOn() && unavailableReason == null);
    }

    public String id() {
        return option.id();
    }

    public String name() {
        return option.name();
    }

    public String description() {
        return option.description();
    }

    public boolean available() {
        return unavailableReason == null;
    }

    public Optional<String> unavailableReason() {
        return Optional.ofNullable(unavailableReason);
    }

    /** Whether the user ticked it; an option that is not available is never added, whatever this says. */
    public BooleanProperty selectedProperty() {
        return selected;
    }

    /** Whether it is ticked and can be added. */
    public boolean chosen() {
        return available() && selected.get();
    }
}
