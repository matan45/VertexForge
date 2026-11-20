package com.vertexforge.launcher.dialog;

import java.util.List;
import java.util.Objects;

/**
 * What the version question asks before a project opens with an engine that may upgrade it (VK-1690). Toolkit-free,
 * so the open flow builds it in plain unit tests; {@link VersionDialog} only shows it.
 *
 * @param title         the window's title
 * @param header        one line: what is about to happen
 * @param text          why the launcher asks (the compatibility verdict's reason, and what each choice does)
 * @param choices       the buttons, in order; {@link Choice#CANCEL} is always one of them
 * @param defaultChoice the button Enter presses; never a disabled one
 * @param unavailable   why "Back up and open" and "Open a copy" are disabled, or {@code null} when they are not
 */
public record VersionPrompt(String title, String header, String text, List<Choice> choices, Choice defaultChoice,
                            String unavailable) {

    /** One answer. */
    public enum Choice {
        BACK_UP_AND_OPEN("Back up and open"),
        OPEN_COPY("Open a copy"),
        OPEN_ANYWAY("Open anyway"),
        /** "Open anyway" under the name a warning gives it. */
        OPEN("Open"),
        CANCEL("Cancel");

        private final String label;

        Choice(String label) {
            this.label = label;
        }

        public String label() {
            return label;
        }

        /** Whether this choice needs a backup or a copy of the project folder. */
        public boolean needsFolderCopy() {
            return this == BACK_UP_AND_OPEN || this == OPEN_COPY;
        }
    }

    public VersionPrompt {
        Objects.requireNonNull(title, "title");
        Objects.requireNonNull(header, "header");
        Objects.requireNonNull(text, "text");
        choices = List.copyOf(choices);
        Objects.requireNonNull(defaultChoice, "defaultChoice");
        if (!choices.contains(Choice.CANCEL)) {
            throw new IllegalArgumentException("A version question can always be cancelled");
        }
        if (!choices.contains(defaultChoice)) {
            throw new IllegalArgumentException("The default " + defaultChoice + " is not one of " + choices);
        }
        if (unavailable != null && defaultChoice.needsFolderCopy()) {
            throw new IllegalArgumentException("The default " + defaultChoice + " is disabled: " + unavailable);
        }
    }

    /** Whether {@code choice} is offered and can be pressed. */
    public boolean enabled(Choice choice) {
        return choices.contains(choice) && (unavailable == null || !choice.needsFolderCopy());
    }
}
