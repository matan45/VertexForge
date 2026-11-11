package com.vertexforge.launcher.env;

import java.io.IOException;

/**
 * Starts a process from a fully configured {@link ProcessBuilder}.
 * <p>
 * Production code uses {@link #system()}; tests pass a fake that records the builder and returns a scripted
 * {@link Process}. Callers keep configuring the builder (command, directory, redirects), so changing how a
 * process is started never changes this interface.
 */
@FunctionalInterface
public interface ProcessStarter {

    Process start(ProcessBuilder builder) throws IOException;

    static ProcessStarter system() {
        return ProcessBuilder::start;
    }
}
