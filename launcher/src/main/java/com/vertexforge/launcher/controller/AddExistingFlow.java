package com.vertexforge.launcher.controller;

import com.vertexforge.launcher.model.RecentEntry;
import com.vertexforge.launcher.project.PickedProject;
import com.vertexforge.launcher.repository.AddedProject;
import javafx.beans.property.ReadOnlyBooleanProperty;
import javafx.beans.property.ReadOnlyBooleanWrapper;

import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executor;

/**
 * Adds projects that already exist on disk (VK-1685): picked with "Add Existing…", dropped on the window, or named on
 * the command line of this launcher or of a second one that forwarded it here. Free of the JavaFX toolkit (its
 * property is a plain {@code javafx.beans} one), so it runs in unit tests.
 * <p>
 * Every request waits in one queue. Requests run one at a time, in order, and only while the window is free (see
 * {@link Services#busy()}): not before the list was first shown, and not during a launch, an engine choice or a
 * dialog. A request's project file is read in the background (a folder holding several asks which one), then added on
 * the store thread; an Open request then opens it. Once the queue is empty, one report says what was added and why
 * anything was refused.
 * <p>
 * Everything here runs on the FX thread (in tests, the test thread); {@code fx} brings background answers back to it.
 */
public final class AddExistingFlow {

    /**
     * One request.
     *
     * @param path a project file, or a folder holding one
     * @param open whether to open the project once it is listed ({@code --open}, a bare path)
     */
    public record Item(Path path, boolean open) {
        public Item {
            Objects.requireNonNull(path, "path");
        }
    }

    /** What the flow needs from the window. Only {@link #choose} may block (it shows a dialog). */
    public interface Services {
        /** Reads the project file or folder in the background; never completes exceptionally. */
        CompletableFuture<PickedProject> inspect(Path path);

        /** Adds the project on the store thread. */
        CompletableFuture<AddedProject> add(PickedProject.Found found);

        /** Asks which project of a folder to add; empty when the user cancelled. */
        Optional<PickedProject.Found> choose(PickedProject.Choose choose);

        /** Whether the list must not be written. */
        boolean readOnly();

        /** Whether the window cannot take a request now; the window calls {@link #drain()} once it can. */
        boolean busy();

        /** Shows the list as written and selects the project. */
        void added(AddedProject added);

        /** The list could not be written. */
        void saveFailed(Throwable error);

        /**
         * Opens the project.
         *
         * @param entry its entry, or {@code null} when it is not listed
         * @return {@code false} when nothing was started because another launch is in progress
         */
        boolean open(Path projectFile, RecentEntry entry, String name);

        /** Says what the requests since the last report did. */
        void report(Report report);
    }

    /**
     * What a run of requests did.
     *
     * @param added         names of the projects that were added
     * @param alreadyListed names of the projects that were listed already
     * @param notAdded      names of the projects that could not be added because the list is read-only
     * @param notOpened     names of the projects that were added but not opened, because another launch was running
     * @param refused       the requests that were refused, with the reason
     */
    public record Report(List<String> added, List<String> alreadyListed, List<String> notAdded,
                         List<String> notOpened, List<Refusal> refused) {
        public Report {
            added = List.copyOf(added);
            alreadyListed = List.copyOf(alreadyListed);
            notAdded = List.copyOf(notAdded);
            notOpened = List.copyOf(notOpened);
            refused = List.copyOf(refused);
        }

        /** A refused request: the path it named and why. */
        public record Refusal(Path path, String reason) {
        }

        public boolean isEmpty() {
            return added.isEmpty() && alreadyListed.isEmpty() && notAdded.isEmpty() && notOpened.isEmpty()
                    && refused.isEmpty();
        }

        /** Whether the summary reports something that did not happen as asked. */
        public boolean hasWarning() {
            return !notAdded.isEmpty() || !notOpened.isEmpty();
        }

        /** One line for a banner, or empty when only refusals (or nothing) happened. */
        public Optional<String> summary() {
            List<String> parts = new ArrayList<>();
            if (!added.isEmpty()) {
                parts.add("Added " + quoted(added) + ".");
            }
            if (!alreadyListed.isEmpty()) {
                parts.add(quoted(alreadyListed) + (alreadyListed.size() == 1 ? " is" : " are")
                        + " already in the list.");
            }
            if (!notAdded.isEmpty()) {
                parts.add("The recent-projects list is read-only here, so " + quoted(notAdded)
                        + (notAdded.size() == 1 ? " was" : " were") + " not added.");
            }
            if (!notOpened.isEmpty()) {
                parts.add(quoted(notOpened) + (notOpened.size() == 1 ? " was" : " were")
                        + " not opened because another project was being opened; open it from the list.");
            }
            return parts.isEmpty() ? Optional.empty() : Optional.of(String.join(" ", parts));
        }

        /** The refusals as the text of one error dialog, or empty when nothing was refused. */
        public Optional<String> refusals() {
            if (refused.isEmpty()) {
                return Optional.empty();
            }
            if (refused.size() == 1) {
                Refusal only = refused.getFirst();
                return Optional.of("Could not add " + only.path() + ":\n" + only.reason());
            }
            StringBuilder text = new StringBuilder("Could not add " + refused.size() + " items:\n");
            for (Refusal refusal : refused) {
                text.append('\n').append(refusal.path()).append(": ").append(refusal.reason());
            }
            return Optional.of(text.toString());
        }

        /** {@code "A"}, {@code "A" and "B"}, {@code "A", "B" and "C"}. */
        static String quoted(List<String> names) {
            List<String> quoted = names.stream().map(name -> "\"" + name + "\"").toList();
            if (quoted.size() == 1) {
                return quoted.getFirst();
            }
            return String.join(", ", quoted.subList(0, quoted.size() - 1)) + " and " + quoted.getLast();
        }
    }

    private final Services services;
    private final Executor fx;
    private final Deque<Item> queue = new ArrayDeque<>();
    /** A request is running or waiting. */
    private final ReadOnlyBooleanWrapper busy = new ReadOnlyBooleanWrapper(false);
    private boolean running;

    private final List<String> added = new ArrayList<>();
    private final List<String> alreadyListed = new ArrayList<>();
    private final List<String> notAdded = new ArrayList<>();
    private final List<String> notOpened = new ArrayList<>();
    private final List<Report.Refusal> refused = new ArrayList<>();

    /** @param fx runs the background answers on the FX thread */
    public AddExistingFlow(Services services, Executor fx) {
        this.services = Objects.requireNonNull(services, "services");
        this.fx = Objects.requireNonNull(fx, "fx");
    }

    /** Whether a request is running or waiting. */
    public ReadOnlyBooleanProperty busyProperty() {
        return busy.getReadOnlyProperty();
    }

    public boolean isBusy() {
        return busy.get();
    }

    /** Queues the requests behind any that are waiting, and starts the next one if the window is free. */
    public void submit(List<Item> items) {
        queue.addAll(List.copyOf(items));
        updateBusy();
        drain();
    }

    /**
     * Starts the next request if none is running and the window is free; once nothing is left, reports. The window
     * calls this whenever it may have become free.
     */
    public void drain() {
        if (running) {
            return;
        }
        if (queue.isEmpty()) {
            flushReport();
            return;
        }
        if (services.busy()) {
            return;
        }
        running = true;
        updateBusy();
        run(queue.removeFirst());
    }

    private void run(Item item) {
        CompletableFuture<PickedProject> inspected;
        try {
            inspected = services.inspect(item.path());
        } catch (RuntimeException e) {
            refuse(item.path(), "Could not be checked: " + describe(e));
            finish();
            return;
        }
        inspected.whenCompleteAsync((picked, error) -> {
            if (error != null || picked == null) {
                refuse(item.path(), "Could not be checked: " + describe(error));
                finish();
                return;
            }
            handle(item, picked);
        }, fx);
    }

    private void handle(Item item, PickedProject picked) {
        switch (picked) {
            case PickedProject.Found found -> list(item, found);
            case PickedProject.Choose choose -> {
                Optional<PickedProject.Found> chosen = services.choose(choose);
                if (chosen.isPresent()) {
                    list(item, chosen.get());
                } else {
                    // Cancelled: nothing to add and nothing to report.
                    finish();
                }
            }
            case PickedProject.Refused refusal -> {
                refuse(refusal.picked(), refusal.reason());
                finish();
            }
        }
    }

    /** Adds the project, then opens it for an Open request. A read-only list adds nothing, but still opens. */
    private void list(Item item, PickedProject.Found found) {
        String name = found.project().projectName();
        if (services.readOnly()) {
            if (item.open()) {
                open(found.file(), null, name);
            } else {
                notAdded.add(name);
            }
            finish();
            return;
        }

        CompletableFuture<AddedProject> adding;
        try {
            adding = services.add(found);
        } catch (RuntimeException e) {
            saveFailed(item, found, e);
            return;
        }
        adding.whenCompleteAsync((result, error) -> {
            if (error != null || result == null) {
                saveFailed(item, found, error);
                return;
            }
            services.added(result);
            (result.existed() ? alreadyListed : added).add(name);
            if (item.open()) {
                Path listedPath = result.entry().path();
                open(listedPath != null ? listedPath : found.file(), result.entry(), name);
            }
            finish();
        }, fx);
    }

    /** The list could not be written (a newer launcher's list, a locked file): say so, and still open an Open. */
    private void saveFailed(Item item, PickedProject.Found found, Throwable error) {
        services.saveFailed(unwrap(error));
        if (item.open()) {
            open(found.file(), null, found.project().projectName());
        }
        finish();
    }

    private void open(Path projectFile, RecentEntry entry, String name) {
        if (!services.open(projectFile, entry, name)) {
            notOpened.add(name);
        }
    }

    private void refuse(Path path, String reason) {
        refused.add(new Report.Refusal(path, reason));
    }

    private void finish() {
        running = false;
        updateBusy();
        drain();
    }

    private void updateBusy() {
        busy.set(running || !queue.isEmpty());
    }

    private void flushReport() {
        Report report = new Report(added, alreadyListed, notAdded, notOpened, refused);
        if (report.isEmpty()) {
            return;
        }
        added.clear();
        alreadyListed.clear();
        notAdded.clear();
        notOpened.clear();
        refused.clear();
        services.report(report);
    }

    private static Throwable unwrap(Throwable error) {
        Throwable current = error;
        while ((current instanceof CompletionException || current instanceof ExecutionException)
                && current.getCause() != null) {
            current = current.getCause();
        }
        return current;
    }

    private static String describe(Throwable error) {
        if (error == null) {
            return "no answer";
        }
        Throwable cause = unwrap(error);
        return cause.getMessage() != null ? cause.getMessage() : cause.getClass().getSimpleName();
    }
}
