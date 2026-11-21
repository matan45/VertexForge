package com.vertexforge.launcher.ui;

import com.vertexforge.launcher.settings.LauncherState.SavedWindow;
import com.vertexforge.launcher.ui.WindowPlacement.Placement;
import com.vertexforge.launcher.ui.WindowPlacement.Rect;
import javafx.animation.PauseTransition;
import javafx.application.Platform;
import javafx.beans.InvalidationListener;
import javafx.geometry.Rectangle2D;
import javafx.stage.Screen;
import javafx.stage.Stage;
import javafx.util.Duration;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.function.Consumer;

/**
 * Puts the main window back where it was and remembers where it goes (VK-1694). The placement is decided before the
 * window is first shown, so Windows creates it on its own monitor, at that monitor's scaling; the bounds are
 * remembered a second after they stop changing, and once more when the launcher closes. What is remembered is always
 * what {@link WindowTracker} followed, never the stage's bounds at that moment: the launcher can close while it is
 * minimized (the Close after-launch setting), when the stage sits far off screen. FX thread.
 */
public final class WindowStateKeeper {

    /** How long the bounds must stay put before they count. */
    static final Duration SETTLE_DELAY = Duration.millis(300);
    /** How long after that they are written; a window dragged around writes once. */
    static final Duration SAVE_DELAY = Duration.seconds(1);

    private final Stage stage;
    private final WindowTracker tracker;
    private final Consumer<SavedWindow> saver;
    private final double minWidth;
    private final double minHeight;
    private final PauseTransition settle = new PauseTransition(SETTLE_DELAY);
    private final PauseTransition save = new PauseTransition(SAVE_DELAY);
    private boolean settling;
    private Optional<SavedWindow> lastSaved;

    private WindowStateKeeper(Stage stage, WindowTracker tracker, Consumer<SavedWindow> saver, double minWidth,
                              double minHeight, Optional<SavedWindow> saved) {
        this.stage = stage;
        this.tracker = tracker;
        this.saver = saver;
        this.minWidth = minWidth;
        this.minHeight = minHeight;
        this.lastSaved = saved;
        settle.setOnFinished(event -> settleNow());
        save.setOnFinished(event -> saveIfChanged());
    }

    /**
     * Places {@code stage} (which is not showing yet) where it was, or centred on the primary screen when that place is
     * on no screen there is now, and starts following it.
     *
     * @param saved     where the window was last time
     * @param minWidth  the launcher's minimum width; a smaller screen lowers it
     * @param minHeight likewise
     * @param saver     writes the bounds; called on the FX thread, it hands the write to the store thread
     */
    public static WindowStateKeeper restore(Stage stage, Optional<SavedWindow> saved, double minWidth, double minHeight,
                                            Consumer<SavedWindow> saver) {
        Objects.requireNonNull(stage, "stage");
        Objects.requireNonNull(saved, "saved");
        Objects.requireNonNull(saver, "saver");
        Placement placement = WindowPlacement.fit(saved, screens(), minWidth, minHeight, MainView.DEFAULT_WIDTH,
                MainView.DEFAULT_HEIGHT);
        apply(stage, placement);
        if (placement.maximized()) {
            // On top of the normal bounds, so that un-maximizing returns to them.
            stage.setMaximized(true);
        }
        WindowStateKeeper keeper = new WindowStateKeeper(stage, new WindowTracker(placement.bounds(),
                placement.maximized()), saver, minWidth, minHeight, saved);
        keeper.follow();
        return keeper;
    }

    /**
     * Checks the window once it is showing: a monitor with another scaling can make Windows resize or move a new
     * window, so one that ended up off every screen is fitted again.
     */
    public void afterShown() {
        Platform.runLater(() -> {
            if (!stage.isShowing() || stage.isMaximized() || stage.isIconified()) {
                return;
            }
            Rect now = bounds();
            if (!SavedWindow.valid(now.x(), now.y(), now.width(), now.height())) {
                return;
            }
            Placement placement = WindowPlacement.fit(Optional.of(new SavedWindow(now.x(), now.y(), now.width(),
                    now.height(), false)), screens(), minWidth, minHeight, MainView.DEFAULT_WIDTH,
                    MainView.DEFAULT_HEIGHT);
            if (!placement.bounds().equals(now)) {
                apply(stage, placement);
            }
        });
    }

    /** Remembers the window now, with anything still settling (the launcher is closing). */
    public void saveNow() {
        if (settling) {
            settle.stop();
            settleNow();
        }
        save.stop();
        saveIfChanged();
    }

    private void follow() {
        InvalidationListener moved = observable -> {
            tracker.boundsChanged(bounds());
            settling = true;
            settle.playFromStart();
        };
        stage.xProperty().addListener(moved);
        stage.yProperty().addListener(moved);
        stage.widthProperty().addListener(moved);
        stage.heightProperty().addListener(moved);
        stage.maximizedProperty().addListener(observable -> {
            tracker.maximizedChanged(stage.isMaximized(), stage.isIconified());
            save.playFromStart();
        });
    }

    private void settleNow() {
        settling = false;
        tracker.settled(stage.isMaximized(), stage.isIconified());
        save.playFromStart();
    }

    private void saveIfChanged() {
        Optional<SavedWindow> state = tracker.state();
        if (state.isPresent() && !state.equals(lastSaved)) {
            lastSaved = state;
            saver.accept(state.get());
        }
    }

    private Rect bounds() {
        return new Rect(stage.getX(), stage.getY(), stage.getWidth(), stage.getHeight());
    }

    private static void apply(Stage stage, Placement placement) {
        stage.setMinWidth(placement.minWidth());
        stage.setMinHeight(placement.minHeight());
        Rect bounds = placement.bounds();
        stage.setX(bounds.x());
        stage.setY(bounds.y());
        stage.setWidth(bounds.width());
        stage.setHeight(bounds.height());
    }

    /** Every screen's usable area, the primary screen first. */
    private static List<Rect> screens() {
        List<Rect> screens = new ArrayList<>();
        Screen primary = Screen.getPrimary();
        screens.add(rect(primary.getVisualBounds()));
        for (Screen screen : Screen.getScreens()) {
            if (!screen.equals(primary)) {
                screens.add(rect(screen.getVisualBounds()));
            }
        }
        return screens;
    }

    private static Rect rect(Rectangle2D bounds) {
        return new Rect(bounds.getMinX(), bounds.getMinY(), bounds.getWidth(), bounds.getHeight());
    }
}
