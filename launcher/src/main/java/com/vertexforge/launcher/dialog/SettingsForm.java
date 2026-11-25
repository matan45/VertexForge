package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.settings.LauncherSettings;
import com.vertexforge.launcher.settings.LauncherSettings.AfterLaunch;
import com.vertexforge.launcher.settings.LauncherSettingsRepository;
import javafx.beans.Observable;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.ObjectProperty;
import javafx.beans.property.ReadOnlyBooleanProperty;
import javafx.beans.property.ReadOnlyBooleanWrapper;
import javafx.beans.property.ReadOnlyStringProperty;
import javafx.beans.property.ReadOnlyStringWrapper;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.beans.property.SimpleObjectProperty;
import javafx.beans.property.SimpleStringProperty;
import javafx.beans.property.StringProperty;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.OptionalInt;
import java.util.UUID;

/**
 * Everything the Settings dialog does, without its controls (VK-1694), so it runs in unit tests without the JavaFX
 * toolkit. Every field is checked as it is typed, nothing here touches the disk, and {@link #result()} says what Save
 * writes: the settings file, the engines file's default engine (one source: the same value the Engines dialog's "Set
 * as Default" writes), and how many of the oldest unpinned projects a lowered cap removes from the list at once.
 */
public final class SettingsForm {

    /** Shown for the choice that clears the default engine. */
    public static final String NO_DEFAULT_ENGINE = "None (use the bundled engine, or ask)";

    /**
     * One default-engine choice.
     *
     * @param engineId the registered engine, or empty for {@link #NO_DEFAULT_ENGINE}
     * @param label    what the choice shows
     */
    public record EngineItem(Optional<UUID> engineId, String label) {
        public EngineItem {
            Objects.requireNonNull(engineId, "engineId");
            Objects.requireNonNull(label, "label");
        }

        @Override
        public String toString() {
            return label;
        }
    }

    /** A new default engine for {@code engines.json}; empty clears the default. */
    public record DefaultEngineChange(Optional<UUID> engineId) {
        public DefaultEngineChange {
            Objects.requireNonNull(engineId, "engineId");
        }
    }

    /**
     * What Save does.
     *
     * @param settings         the settings to write
     * @param defaultEngine    the default engine to set, when it changed
     * @param projectsToRemove how many of the oldest unpinned projects the cap removes from the list now (0 when the
     *                         list is read-only)
     */
    public record Result(LauncherSettings settings, Optional<DefaultEngineChange> defaultEngine,
                         int projectsToRemove) {
        public Result {
            Objects.requireNonNull(settings, "settings");
            Objects.requireNonNull(defaultEngine, "defaultEngine");
        }
    }

    private final LauncherSettings current;
    private final boolean canSave;
    private final boolean listReadOnly;
    private final int listed;
    private final int pinned;
    private final List<EngineItem> engineItems;
    private final Optional<UUID> currentDefault;
    private final boolean engineChoiceEnabled;
    private final String engineNote;

    private final StringProperty locationText = new SimpleStringProperty(this, "locationText", "");
    private final StringProperty limitText = new SimpleStringProperty(this, "limitText", "");
    private final StringProperty retentionText = new SimpleStringProperty(this, "retentionText", "");
    private final ObjectProperty<AfterLaunch> afterLaunch = new SimpleObjectProperty<>(this, "afterLaunch");
    private final BooleanProperty showEditorConsole = new SimpleBooleanProperty(this, "showEditorConsole");
    private final ObjectProperty<EngineItem> defaultEngine = new SimpleObjectProperty<>(this, "defaultEngine");

    private final ReadOnlyStringWrapper locationProblem = new ReadOnlyStringWrapper(this, "locationProblem", "");
    private final ReadOnlyStringWrapper limitProblem = new ReadOnlyStringWrapper(this, "limitProblem", "");
    private final ReadOnlyStringWrapper retentionProblem = new ReadOnlyStringWrapper(this, "retentionProblem", "");
    private final ReadOnlyStringWrapper trimNotice = new ReadOnlyStringWrapper(this, "trimNotice", "");
    private final ReadOnlyBooleanWrapper saveEnabled = new ReadOnlyBooleanWrapper(this, "saveEnabled");

    /**
     * @param current      the settings as they are now
     * @param canSave      whether the settings file may be written
     * @param listReadOnly whether the recent-projects list is read-only right now (a lowered cap then waits)
     * @param listed       how many projects the list holds
     * @param pinned       how many of them are pinned
     * @param engines      the engines as last checked, or {@code null} while they are not known yet
     */
    public SettingsForm(LauncherSettings current, boolean canSave, boolean listReadOnly, int listed, int pinned,
                        EngineOptions engines) {
        this.current = Objects.requireNonNull(current, "current");
        this.canSave = canSave;
        this.listReadOnly = listReadOnly;
        this.listed = Math.max(0, listed);
        this.pinned = Math.max(0, Math.min(pinned, this.listed));

        engineItems = engineItems(engines);
        currentDefault = engines == null ? Optional.empty() : engines.engines().stream()
                .filter(EngineOption::isDefault)
                .map(EngineOption::id)
                .flatMap(Optional::stream)
                .findFirst();
        engineChoiceEnabled = engines != null && !engines.registry().readOnly();
        if (engines == null) {
            engineNote = "The engines are still being checked; choose the default engine in Engines… or open Settings"
                    + " again.";
        } else if (engines.registry().readOnly()) {
            engineNote = "The engines list cannot be changed by this launcher right now.";
        } else {
            engineNote = "Projects not bound to an engine open with the default engine (the same choice as \"Set as"
                    + " Default\" in Engines…).";
        }

        locationText.set(current.defaultProjectLocation().map(Path::toString).orElse(""));
        limitText.set(Integer.toString(current.recentProjectsLimit()));
        retentionText.set(Integer.toString(current.logRetention()));
        afterLaunch.set(current.afterLaunch());
        showEditorConsole.set(current.showEditorConsole());
        defaultEngine.set(engineItems.stream()
                .filter(item -> item.engineId().equals(currentDefault))
                .findFirst()
                .orElse(engineItems.getFirst()));

        Observable[] inputs = {locationText, limitText, retentionText, afterLaunch, defaultEngine};
        for (Observable input : inputs) {
            input.addListener(observable -> update());
        }
        update();
    }

    private static List<EngineItem> engineItems(EngineOptions engines) {
        List<EngineItem> items = new ArrayList<>();
        items.add(new EngineItem(Optional.empty(), NO_DEFAULT_ENGINE));
        if (engines == null) {
            return List.copyOf(items);
        }
        for (EngineOption engine : engines.engines()) {
            // Only a registered engine can be the default; the current default is kept even when it does not work.
            if (engine.id().isEmpty() || (!engine.isOk() && !engine.isDefault())) {
                continue;
            }
            StringBuilder label = new StringBuilder(engine.versionText());
            if (engine.bundled()) {
                label.append(" (bundled)");
            }
            if (!engine.isOk()) {
                label.append(" (not working)");
            }
            label.append(", ").append(engine.pathText());
            items.add(new EngineItem(engine.id(), label.toString()));
        }
        return List.copyOf(items);
    }

    private void update() {
        locationProblem.set(locationProblem(locationText.get()).orElse(""));
        OptionalInt limit = wholeNumber(limitText.get(), LauncherSettings.MIN_RECENT_PROJECTS_LIMIT,
                LauncherSettings.MAX_RECENT_PROJECTS_LIMIT);
        limitProblem.set(limit.isPresent() ? "" : "Enter a whole number from "
                + LauncherSettings.MIN_RECENT_PROJECTS_LIMIT + " to " + LauncherSettings.MAX_RECENT_PROJECTS_LIMIT
                + ".");
        OptionalInt retention = wholeNumber(retentionText.get(), LauncherSettings.MIN_LOG_RETENTION,
                LauncherSettings.MAX_LOG_RETENTION);
        retentionProblem.set(retention.isPresent() ? "" : "Enter a whole number from "
                + LauncherSettings.MIN_LOG_RETENTION + " to " + LauncherSettings.MAX_LOG_RETENTION + ".");

        int removed = limit.isPresent() ? projectsRemoved(listed, pinned, limit.getAsInt()) : 0;
        if (removed == 0) {
            trimNotice.set("");
        } else if (listReadOnly) {
            trimNotice.set("The list holds more projects than that, but it is read-only right now: nothing is removed"
                    + " until it can be written again.");
        } else {
            trimNotice.set("Saving removes the " + (removed == 1 ? "oldest unpinned project" : removed
                    + " oldest unpinned projects") + " from the list. Pinned projects stay, and no project files are"
                    + " touched.");
        }

        saveEnabled.set(canSave && afterLaunch.get() != null && locationProblem.get().isEmpty()
                && limitProblem.get().isEmpty() && retentionProblem.get().isEmpty());
    }

    /**
     * How many projects a cap of {@code limit} removes from a list of {@code listed} entries, {@code pinned} of which
     * are pinned: the oldest unpinned ones beyond the cap; pinned ones are never removed.
     */
    public static int projectsRemoved(int listed, int pinned, int limit) {
        return Math.max(0, Math.min(listed - pinned, listed - limit));
    }

    /** Why {@code text} cannot be the default project location; empty when it can (a blank text means none). */
    public static Optional<String> locationProblem(String text) {
        if (text == null || text.isBlank()) {
            return Optional.empty();
        }
        return LauncherSettingsRepository.absolutePath(text.strip()).isPresent()
                ? Optional.empty()
                : Optional.of("Enter the full path of a folder, such as D:\\Projects, or leave it empty.");
    }

    /** {@code text} as a whole number from {@code min} to {@code max}; empty when it is not one. */
    static OptionalInt wholeNumber(String text, int min, int max) {
        if (text == null) {
            return OptionalInt.empty();
        }
        String digits = text.strip();
        if (digits.isEmpty() || digits.length() > 9 || !digits.chars().allMatch(c -> c >= '0' && c <= '9')) {
            return OptionalInt.empty();
        }
        int value = Integer.parseInt(digits);
        return value < min || value > max ? OptionalInt.empty() : OptionalInt.of(value);
    }

    /** What Save writes; empty while {@link #saveEnabledProperty()} is false. */
    public Optional<Result> result() {
        if (!saveEnabled.get()) {
            return Optional.empty();
        }
        int limit = wholeNumber(limitText.get(), LauncherSettings.MIN_RECENT_PROJECTS_LIMIT,
                LauncherSettings.MAX_RECENT_PROJECTS_LIMIT).orElseThrow();
        int retention = wholeNumber(retentionText.get(), LauncherSettings.MIN_LOG_RETENTION,
                LauncherSettings.MAX_LOG_RETENTION).orElseThrow();
        String location = locationText.get();
        Optional<Path> folder = location == null || location.isBlank()
                ? Optional.empty()
                : LauncherSettingsRepository.absolutePath(location.strip());
        LauncherSettings settings = new LauncherSettings(limit, afterLaunch.get(), showEditorConsole.get(), folder,
                retention);

        Optional<DefaultEngineChange> engineChange = Optional.empty();
        EngineItem chosen = defaultEngine.get();
        if (engineChoiceEnabled && chosen != null && !chosen.engineId().equals(currentDefault)) {
            engineChange = Optional.of(new DefaultEngineChange(chosen.engineId()));
        }
        int removed = listReadOnly ? 0 : projectsRemoved(listed, pinned, limit);
        return Optional.of(new Result(settings, engineChange, removed));
    }

    /** The settings the form started from. */
    public LauncherSettings current() {
        return current;
    }

    public StringProperty locationTextProperty() {
        return locationText;
    }

    public StringProperty limitTextProperty() {
        return limitText;
    }

    public StringProperty retentionTextProperty() {
        return retentionText;
    }

    public ObjectProperty<AfterLaunch> afterLaunchProperty() {
        return afterLaunch;
    }

    public BooleanProperty showEditorConsoleProperty() {
        return showEditorConsole;
    }

    public ObjectProperty<EngineItem> defaultEngineProperty() {
        return defaultEngine;
    }

    /** The default-engine choices: {@link #NO_DEFAULT_ENGINE} first, then every registered engine that works. */
    public List<EngineItem> engineItems() {
        return engineItems;
    }

    /** Whether the default engine can be chosen here: the engines are known and their file may be written. */
    public boolean engineChoiceEnabled() {
        return engineChoiceEnabled;
    }

    /** What the default engine means, or why it cannot be chosen now. */
    public String engineNote() {
        return engineNote;
    }

    /** Whether the settings file may be written at all. */
    public boolean canSave() {
        return canSave;
    }

    public ReadOnlyStringProperty locationProblemProperty() {
        return locationProblem.getReadOnlyProperty();
    }

    public ReadOnlyStringProperty limitProblemProperty() {
        return limitProblem.getReadOnlyProperty();
    }

    public ReadOnlyStringProperty retentionProblemProperty() {
        return retentionProblem.getReadOnlyProperty();
    }

    /** What a lowered cap does to the list; empty when it removes nothing. */
    public ReadOnlyStringProperty trimNoticeProperty() {
        return trimNotice.getReadOnlyProperty();
    }

    public ReadOnlyBooleanProperty saveEnabledProperty() {
        return saveEnabled.getReadOnlyProperty();
    }
}
