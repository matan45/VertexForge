package com.vertexforge.launcher.service;

import com.vertexforge.launcher.model.ProjectCreationResult;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.store.StoreWriteException.Reason;

import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * One project creation as the window runs it (VK-1679): the files on the creation thread, then the recent-list add
 * on the store thread, so that list changes stay in order and a slow creation never holds the list up.
 * <p>
 * The add is handed to the store thread from the creation thread itself, never through the FX thread: while the
 * launcher closes, the FX thread waits for both threads and runs nothing else, so a created project would otherwise
 * never be listed. When the store thread has already stopped, the add runs on the creation thread.
 */
public final class CreationJob {

    /** Hears about each file written. Called on the creation thread. */
    @FunctionalInterface
    public interface ProgressListener {
        ProgressListener NONE = (done, total) -> {
        };

        void progress(int done, int total);
    }

    private final String name;
    private final AtomicBoolean cancelRequested = new AtomicBoolean();
    private final CompletableFuture<CreateOutcome> outcome = new CompletableFuture<>();

    private CreationJob(String name) {
        this.name = name;
    }

    /**
     * Starts creating {@code request} on {@code creation}; the add to the list then runs on {@code store}.
     * Returns at once.
     */
    public static CreationJob start(ProjectHub hub, CreateRequest request, Executor creation, Executor store,
                                    ProgressListener listener) {
        Objects.requireNonNull(hub, "hub");
        Objects.requireNonNull(request, "request");
        Objects.requireNonNull(creation, "creation");
        Objects.requireNonNull(store, "store");
        Objects.requireNonNull(listener, "listener");

        CreationJob job = new CreationJob(request.name());
        try {
            creation.execute(() -> job.run(hub, request, store, listener));
        } catch (RejectedExecutionException e) {
            job.outcome.complete(CreateOutcome.failed(ProjectCreationResult.failure("The launcher is closing.")));
        }
        return job;
    }

    /** The project's name as typed. */
    public String name() {
        return name;
    }

    /**
     * Asks the creation to stop. It stops at the next file, deleting what it wrote; once the project is being moved
     * into place it finishes anyway, so the outcome may still be a created project.
     */
    public void cancel() {
        cancelRequested.set(true);
    }

    public boolean cancelRequested() {
        return cancelRequested.get();
    }

    /** What happened. Never completes exceptionally. */
    public CompletableFuture<CreateOutcome> outcome() {
        return outcome;
    }

    private void run(ProjectHub hub, CreateRequest request, Executor store, ProgressListener listener) {
        boolean handedToStore = false;
        try {
            ProjectCreationResult result;
            try {
                result = hub.createProjectFiles(request, new CreationMonitor() {
                    @Override
                    public void progress(int done, int total) {
                        listener.progress(done, total);
                    }

                    @Override
                    public boolean cancelRequested() {
                        return cancelRequested.get();
                    }
                });
            } catch (RuntimeException e) {
                result = ProjectCreationResult.failure("Could not create the project: " + e);
            }

            if (!result.success()) {
                outcome.complete(CreateOutcome.failed(result));
                return;
            }

            ProjectCreationResult created = result;
            try {
                store.execute(() -> list(hub, created));
            } catch (RejectedExecutionException e) {
                // The launcher is closing and its store thread is gone. List the project here instead: nothing waits
                // for the order of list changes any more, and the store's own locks keep the file safe.
                list(hub, created);
            }
            handedToStore = true;
        } finally {
            // Even an Error must end the job: the window keeps Create disabled until it does. (Once the list step is
            // under way, it completes the outcome itself.)
            if (!handedToStore) {
                outcome.complete(CreateOutcome.failed(
                        ProjectCreationResult.failure("Project creation stopped unexpectedly.")));
            }
        }
    }

    private void list(ProjectHub hub, ProjectCreationResult created) {
        CreateOutcome listed = null;
        try {
            listed = hub.listCreated(created, name);
        } catch (RuntimeException e) {
            listed = notListed(created, "Could not add the project to the list: " + e, e);
        } finally {
            outcome.complete(listed != null ? listed
                    : notListed(created, "Adding the project to the list stopped unexpectedly.", null));
        }
    }

    private static CreateOutcome notListed(ProjectCreationResult created, String message, Throwable cause) {
        return CreateOutcome.notListed(created, ProjectHub.canonical(created.projectPath()),
                new StoreWriteException(Reason.IO, message, cause));
    }
}
