package com.vertexforge.launcher.ui;

import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.VBox;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Objects;

/**
 * Messages across the top of the main window (VK-1678): a damaged or read-only recent-projects list, a save that
 * failed. Each message has a key, and showing a key again replaces its message instead of stacking another one.
 * The box is hidden (and takes no space) while it is empty. Used on the FX thread only.
 */
public final class BannerBar {

    public enum Kind {
        INFO("banner-info"),
        WARNING("banner-warning"),
        ERROR("banner-error");

        private final String styleClass;

        Kind(String styleClass) {
            this.styleClass = styleClass;
        }
    }

    private final VBox box;
    private final Map<String, Node> banners = new LinkedHashMap<>();

    public BannerBar(VBox box) {
        this.box = Objects.requireNonNull(box, "box");
        refreshVisibility();
    }

    /** Shows (or replaces) the banner for {@code key}; a dismissable one has a close button. */
    public void show(String key, Kind kind, String message, boolean dismissable) {
        Label text = new Label(message);
        text.setWrapText(true);
        text.setMaxWidth(Double.MAX_VALUE);
        text.getStyleClass().add("banner-text");
        HBox.setHgrow(text, Priority.ALWAYS);

        HBox banner = new HBox(text);
        banner.getStyleClass().addAll("banner", kind.styleClass);
        if (dismissable) {
            Button close = new Button("×");
            close.getStyleClass().add("banner-close");
            close.setOnAction(event -> hide(key));
            banner.getChildren().add(close);
        }

        Node previous = banners.put(key, banner);
        if (previous != null) {
            int index = box.getChildren().indexOf(previous);
            box.getChildren().set(index, banner);
        } else {
            box.getChildren().add(banner);
        }
        refreshVisibility();
    }

    public void hide(String key) {
        Node banner = banners.remove(key);
        if (banner != null) {
            box.getChildren().remove(banner);
            refreshVisibility();
        }
    }

    public boolean isShowing(String key) {
        return banners.containsKey(key);
    }

    /** The message shown for {@code key}, or {@code null}. */
    public String messageOf(String key) {
        Node banner = banners.get(key);
        if (banner instanceof HBox row && !row.getChildren().isEmpty() && row.getChildren().getFirst() instanceof Label label) {
            return label.getText();
        }
        return null;
    }

    private void refreshVisibility() {
        boolean any = !box.getChildren().isEmpty();
        box.setVisible(any);
        box.setManaged(any);
    }
}
