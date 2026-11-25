package com.vertexforge.launcher.service;

import com.vertexforge.launcher.env.ProcessStarter;

import java.io.IOException;
import java.nio.file.Path;
import java.util.List;
import java.util.Objects;

/**
 * Shows files and folders in Windows (VK-1683): the failure dialog's Open log and Open crash folder. It goes through
 * {@code explorer.exe}, started through the launcher's {@link ProcessStarter} so tests see the command. Explorer exits
 * with 1 even when it worked, so its exit code is never read.
 */
public final class ShellOpener {

    public static final String EXPLORER = "explorer.exe";

    private final ProcessStarter starter;

    public ShellOpener(ProcessStarter starter) {
        this.starter = Objects.requireNonNull(starter, "starter");
    }

    /** Opens a file with its default program, or a folder in Explorer. */
    public void open(Path target) throws IOException {
        start(List.of(EXPLORER, target.toString()));
    }

    /** Opens the folder of {@code file} in Explorer with {@code file} selected. */
    public void showInFolder(Path file) throws IOException {
        // "/select," and the path as two arguments: Java quotes a path with spaces, which Explorer accepts there.
        start(List.of(EXPLORER, "/select,", file.toString()));
    }

    private void start(List<String> command) throws IOException {
        ProcessBuilder builder = new ProcessBuilder(command);
        builder.redirectOutput(ProcessBuilder.Redirect.DISCARD);
        builder.redirectError(ProcessBuilder.Redirect.DISCARD);
        starter.start(builder);
    }
}
