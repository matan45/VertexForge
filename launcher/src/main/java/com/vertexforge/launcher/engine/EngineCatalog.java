package com.vertexforge.launcher.engine;

import com.vertexforge.launcher.env.LauncherEnvironment;
import com.vertexforge.launcher.store.StoreWriteException;
import com.vertexforge.launcher.util.LauncherLog;
import com.vertexforge.launcher.util.SingleFlightChecks;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;

/**
 * Every engine the launcher knows (VK-1681): the registry in {@code engines.json}, the engine bundled with the running
 * launcher and the {@code VERTEXFORGE_EDITOR_PATH} override. It replaces the old editor search, which probed fixed
 * folders below the working directory.
 * <p>
 * Free of JavaFX. {@link #load} and the changes block on the engines file, so they run on the store thread; the
 * engine checks run on their own threads with a timeout.
 */
public final class EngineCatalog implements AutoCloseable {

    private final EngineRegistryRepository repository;
    private final EngineProbe probe;
    private final EngineHealthChecker checker;
    private final SingleFlightChecks<Path, PickedEngine> pickChecks;
    private final Optional<Path> expectedBundledExecutable;
    private final Optional<EditorOverride> override;
    private final LauncherLog log;

    /**
     * @param expectedBundledExecutable where the running launcher's bundled editor would be (see
     *                                  {@link BundledEngine#expectedExecutable}); empty when the launcher is not
     *                                  packaged
     */
    public EngineCatalog(EngineRegistryRepository repository, EngineProbe probe, Duration timeout,
                         Optional<Path> expectedBundledExecutable, Optional<EditorOverride> override,
                         LauncherLog log) {
        this.repository = Objects.requireNonNull(repository, "repository");
        this.probe = Objects.requireNonNull(probe, "probe");
        Objects.requireNonNull(timeout, "timeout");
        this.expectedBundledExecutable = Objects.requireNonNull(expectedBundledExecutable,
                "expectedBundledExecutable");
        this.override = Objects.requireNonNull(override, "override");
        this.log = Objects.requireNonNull(log, "log");
        this.checker = new EngineHealthChecker(probe, timeout);
        this.pickChecks = new SingleFlightChecks<>(
                "vf-engine-pick-",
                timeout,
                picked -> EngineProbe.inspectPicked(picked, probe),
                new PickedEngine.Refused("Could not check the engine"),
                e -> new PickedEngine.Refused("Could not check the engine: " + e),
                new PickedEngine.Refused("No response after " + SingleFlightChecks.describe(timeout)
                        + ": the file's drive or network share may be unreachable"),
                new PickedEngine.Refused("The launcher is shutting down"));
    }

    /** The catalog for an environment: {@code configDir/engines.json}, its launcher's bundled engine and override. */
    public static EngineCatalog create(LauncherEnvironment environment, LauncherLog log) {
        EngineRegistryRepository repository = new EngineRegistryRepository(
                EngineRegistryRepository.openStore(
                        environment.configDir().resolve(EngineRegistryRepository.FILE_NAME), environment.clock(), log),
                UUID::randomUUID, log);
        return new EngineCatalog(
                repository,
                EngineProbe.system(),
                EngineHealthChecker.DEFAULT_TIMEOUT,
                environment.launcherExecutable().flatMap(BundledEngine::expectedExecutable),
                EditorOverride.from(environment.env()),
                log);
    }

    public Path file() {
        return repository.file();
    }

    /**
     * Reads the registry. When the running launcher's bundled engine is on disk and not registered yet, it is
     * registered first (nothing is written when it already is), so projects can be bound to it like to any other
     * engine. A registry that may not be written still offers it, unsaved ({@link #check}). Store thread; never
     * throws.
     */
    public EngineListing load() {
        EngineRegistryView read = repository.load();
        EngineRegistryView view = read;
        Optional<Path> bundled = bundledExecutableOnDisk();
        if (bundled.isPresent() && !read.readOnly() && read.findByPath(bundled.get()).isEmpty()) {
            try {
                EngineRegistryView written = repository.add(bundled.get()).view();
                // A read that set a corrupt file aside must still say so: the write after it found no file at all.
                view = read.quarantinedTo().isEmpty() ? written : new EngineRegistryView(written.engines(),
                        written.defaultEngineId(), read.status(), read.quarantinedTo(), read.detail(),
                        written.skippedElements());
            } catch (StoreWriteException e) {
                // Logged by the repository; the engine is still offered, unsaved.
            }
        }
        return new EngineListing(view, bundled);
    }

    /**
     * Checks every engine in {@code listing} in the background. The result never completes exceptionally: an engine
     * whose check fails is Broken.
     */
    public CompletableFuture<EngineOptions> check(EngineListing listing) {
        EngineRegistryView registry = listing.registry();
        Optional<Path> bundled = listing.bundledExecutable();
        Optional<UUID> defaultId = registry.defaultEngineId();

        List<CompletableFuture<EngineOption>> pending = new ArrayList<>();
        for (EngineInstall install : registry.engines()) {
            boolean isBundled = bundled.isPresent() && bundled.get().equals(install.executable());
            boolean isDefault = defaultId.isPresent() && defaultId.get().equals(install.id());
            pending.add(health(install.executable()).thenApply(health -> new EngineOption(Optional.of(install.id()),
                    install.executable(), install.rawPath(), health, isBundled, isDefault, false)));
        }
        if (bundled.isPresent() && registry.findByPath(bundled.get()).isEmpty()) {
            Path executable = bundled.get();
            pending.add(health(executable).thenApply(health -> new EngineOption(Optional.empty(), executable,
                    executable.toString(), health, true, false, false)));
        }
        CompletableFuture<Optional<EngineOption>> overrideOption = override
                .map(value -> health(value.executable()).thenApply(health -> Optional.of(new EngineOption(
                        Optional.empty(), value.executable(), value.rawValue(),
                        value.executable() == null
                                ? EngineHealth.broken(EditorOverride.ENV_VAR + " is not an absolute path to an editor")
                                : health,
                        false, false, true))))
                .orElse(CompletableFuture.completedFuture(Optional.empty()));

        CompletableFuture<?>[] all = new CompletableFuture<?>[pending.size() + 1];
        for (int i = 0; i < pending.size(); i++) {
            all[i] = pending.get(i);
        }
        all[pending.size()] = overrideOption;
        return CompletableFuture.allOf(all).thenApply(done -> new EngineOptions(
                registry,
                overrideOption.join(),
                pending.stream().map(CompletableFuture::join).toList()));
    }

    /**
     * Turns a file picked with "Add Engine…" into an engine, in the background with a timeout. Nothing is
     * registered yet: {@link #add} does that.
     */
    public CompletableFuture<PickedEngine> inspect(Path picked) {
        if (picked == null) {
            return CompletableFuture.completedFuture(new PickedEngine.Refused("No file was chosen"));
        }
        return pickChecks.check(picked.toAbsolutePath().normalize());
    }

    /** Registers an engine {@link #inspect} found usable. Store thread. */
    public EngineRegistryRepository.Added add(PickedEngine.Usable engine) throws StoreWriteException {
        return repository.add(engine.realExecutable());
    }

    /** Unregisters an engine; its files are never touched. Store thread. */
    public EngineRegistryView remove(UUID id) throws StoreWriteException {
        return repository.remove(id);
    }

    /** Makes {@code id} the default engine; {@code null} clears the default. Store thread. */
    public EngineRegistryView setDefault(UUID id) throws StoreWriteException {
        return repository.setDefault(id);
    }

    /** Stops the check threads. */
    @Override
    public void close() {
        checker.close();
        pickChecks.close();
    }

    /** The health of an editor; a check that fails in an unexpected way is Broken. */
    private CompletableFuture<EngineHealth> health(Path executable) {
        return checker.check(executable).handle((health, error) -> {
            if (error != null || health == null) {
                return EngineHealth.broken("Could not check the engine: " + (error != null ? error : "no answer"));
            }
            return health;
        });
    }

    /** The real path of the bundled editor, when it is on disk. Blocks briefly on the launcher's own folder. */
    private Optional<Path> bundledExecutableOnDisk() {
        if (expectedBundledExecutable.isEmpty()) {
            return Optional.empty();
        }
        Path expected = expectedBundledExecutable.get();
        try {
            if (!Files.isRegularFile(expected)) {
                return Optional.empty();
            }
            return Optional.of(expected.toRealPath());
        } catch (IOException | SecurityException e) {
            log.warn("Could not look for the bundled engine at " + expected + ": " + e.getMessage());
            return Optional.empty();
        }
    }
}
