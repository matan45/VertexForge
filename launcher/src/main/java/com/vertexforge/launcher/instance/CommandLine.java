package com.vertexforge.launcher.instance;

import com.vertexforge.launcher.instance.InstanceRequest.Action;

import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/**
 * The launcher's command line (VK-1685):
 * <pre>
 *   PATH          open the project: add it to the list, then launch it (what a .vfproj double-click passes)
 *   --open PATH   the same
 *   --add PATH    add it to the list only
 * </pre>
 * PATH is a {@code .vfproj} file or a project folder. A relative PATH is resolved against the working directory of
 * the process that was started, since the request may be handed to a launcher running in another folder.
 * <p>
 * Parsing never fails: an argument that cannot be used becomes a problem, which the launcher shows, and every other
 * argument is still used. {@code --self-test} is handled before this parser runs.
 *
 * @param message the requests, in order, and the problems
 */
public record CommandLine(InstanceMessage message) {

    public static final String OPEN = "--open";
    public static final String ADD = "--add";
    private static final String OPTION_PREFIX = "--";

    public CommandLine {
        Objects.requireNonNull(message, "message");
    }

    /**
     * @param args             the arguments as given to {@code main}; {@code null} reads as none
     * @param workingDirectory the absolute folder relative paths are resolved against
     * @throws IllegalArgumentException when {@code workingDirectory} is not absolute
     */
    public static CommandLine parse(String[] args, Path workingDirectory) {
        Objects.requireNonNull(workingDirectory, "workingDirectory");
        if (!workingDirectory.isAbsolute()) {
            throw new IllegalArgumentException("The working directory must be absolute: " + workingDirectory);
        }
        List<InstanceRequest> requests = new ArrayList<>();
        List<String> problems = new ArrayList<>();
        String[] given = args == null ? new String[0] : args;

        for (int i = 0; i < given.length; i++) {
            String argument = given[i];
            if (argument == null) {
                continue;
            }
            if (argument.equals(OPEN) || argument.equals(ADD)) {
                Action action = argument.equals(OPEN) ? Action.OPEN : Action.ADD;
                // A following option is never taken as the path: "--add --open x" is a missing path, then an open.
                if (i + 1 >= given.length || given[i + 1] == null || given[i + 1].startsWith(OPTION_PREFIX)) {
                    problems.add(argument + " needs a path");
                    continue;
                }
                i++;
                addRequest(action, given[i], workingDirectory, requests, problems);
            } else if (argument.startsWith(OPTION_PREFIX)) {
                problems.add("Unknown option " + argument);
            } else {
                addRequest(Action.OPEN, argument, workingDirectory, requests, problems);
            }
        }
        return new CommandLine(new InstanceMessage(requests, problems));
    }

    private static void addRequest(Action action, String text, Path workingDirectory,
                                   List<InstanceRequest> requests, List<String> problems) {
        if (text.isBlank()) {
            problems.add("An empty path was given");
            return;
        }
        Path path;
        try {
            path = Path.of(text);
        } catch (InvalidPathException e) {
            problems.add("\"" + text + "\" is not a valid path: " + e.getReason());
            return;
        }
        // A drive-relative path on another drive ("D:game") stays relative after resolving: it names no folder.
        Path absolute = path.isAbsolute() ? path : workingDirectory.resolve(path);
        if (!absolute.isAbsolute()) {
            problems.add("\"" + text + "\" is not a full path");
            return;
        }
        requests.add(new InstanceRequest(action, absolute.normalize()));
    }
}
