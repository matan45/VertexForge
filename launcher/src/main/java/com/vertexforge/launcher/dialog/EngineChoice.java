package com.vertexforge.launcher.dialog;

import com.vertexforge.launcher.engine.EngineContent;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineResolver;
import com.vertexforge.launcher.engine.LaunchPlan;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

/**
 * An engine the Create Project dialog offers (VK-1689): one that works and whose {@code engine.json} names a templates
 * folder and a script library.
 *
 * @param option  the engine as checked
 * @param content what it offers for new projects
 */
public record EngineChoice(EngineOption option, EngineContent content) {

    public EngineChoice {
        Objects.requireNonNull(option, "option");
        Objects.requireNonNull(content, "content");
    }

    /** {@code 1.0.0 Development (bundled, default)}. */
    public String label() {
        return option.label();
    }

    /** The editor's path. */
    public String path() {
        return option.pathText();
    }

    /** The engines projects can be created from: an active override first, then the registered engines in order. */
    public static List<EngineChoice> from(EngineOptions options) {
        List<EngineChoice> choices = new ArrayList<>();
        options.activeOverride().flatMap(EngineChoice::of).ifPresent(choices::add);
        for (EngineOption option : options.engines()) {
            of(option).ifPresent(choices::add);
        }
        return List.copyOf(choices);
    }

    /**
     * The choice to select first: the engine a new project would open with ({@link EngineResolver}, so an active
     * override, else the default, the bundled or the only engine) when it is offered; else the only choice; else none.
     */
    public static Optional<EngineChoice> preselect(EngineOptions options, List<EngineChoice> choices) {
        if (EngineResolver.resolve(options, null) instanceof LaunchPlan.Ready ready) {
            for (EngineChoice choice : choices) {
                if (choice.option().equals(ready.engine())) {
                    return Optional.of(choice);
                }
            }
        }
        return choices.size() == 1 ? Optional.of(choices.getFirst()) : Optional.empty();
    }

    /**
     * Why no engine can be offered, for options that gave no {@link #from choice}: none works, or none of those that
     * work has the content.
     */
    public static String noChoiceReason(EngineOptions options) {
        List<EngineOption> working = new ArrayList<>();
        options.activeOverride().ifPresent(working::add);
        working.addAll(options.usable());
        if (working.isEmpty()) {
            return EngineResolver.NO_ENGINE;
        }
        StringBuilder text = new StringBuilder("None of the working engines can create projects:");
        for (EngineOption option : working) {
            text.append("\n• ").append(option.label()).append(", ").append(option.pathText()).append(": ")
                    .append(EngineContent.unavailableReason(option).orElse("it offers nothing to create from"))
                    .append('.');
        }
        return text.toString();
    }

    private static Optional<EngineChoice> of(EngineOption option) {
        return EngineContent.of(option).map(content -> new EngineChoice(option, content));
    }
}
