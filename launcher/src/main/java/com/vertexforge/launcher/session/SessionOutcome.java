package com.vertexforge.launcher.session;

import com.vertexforge.launcher.service.EditorExitCode;

import java.util.Optional;

/**
 * How a supervised editor ended (VK-1683), from its exit code and the last line of its {@code --launch-status} file.
 * The editor's contract ({@code LaunchStatusFile.hpp}): exit code 0 is a normal close whatever the file says;
 * {@code failed} names the reason (an exit code 2 to 5, or a crash's exception code); a non-zero exit while the file
 * still says {@code loading} or {@code ready} means the editor died without saying why.
 */
public sealed interface SessionOutcome {

    /** {@code STATUS_CONTROL_C_EXIT}: the editor's console window was closed, which ends the editor. */
    int CONSOLE_CLOSED_CODE = 0xC000013A;

    /** Exit code 0 after the editor reported its state: the user closed it. */
    record Normal() implements SessionOutcome {
    }

    /**
     * Exit code 0, but the editor never wrote the status file it was asked for: an editor older than the launch
     * contract (VK-1682), which also exits 0 after failing to load a project.
     */
    record NoReadySignal() implements SessionOutcome {
    }

    /** The editor's console window was closed (or the editor got Ctrl+C from another program). */
    record ConsoleClosed() implements SessionOutcome {
    }

    /**
     * The editor refused to open the project: a contract code from 2 to 5.
     *
     * @param code    the {@link EditorExitCode}'s value
     * @param message the editor's reason from the status file, or empty
     */
    record Refused(int code, Optional<String> message) implements SessionOutcome {
    }

    /**
     * The editor's crash handler reported a crash ({@code failed} with an exception code).
     *
     * @param code    the exception code (for example {@code 0xC0000005})
     * @param message the crash handler's text, which names the dump file
     */
    record Crashed(int code, String message) implements SessionOutcome {
    }

    /**
     * The editor ended with a non-zero code without saying why: killed, {@code exit(-1)} from an assertion, a crash
     * of an editor that writes no status file, ...
     *
     * @param exitCode  the process exit code
     * @param lastState the last state the editor reported, or empty when it never wrote one
     */
    record Died(int exitCode, Optional<LaunchStatus.State> lastState) implements SessionOutcome {
    }

    /**
     * @param exitCode        the process exit code
     * @param last            the last status line the editor wrote, when it wrote one
     * @param statusRequested whether the editor was given {@code --launch-status}
     * @param contractKnown   whether the engine advertises a launch contract (so codes 2 to 5 mean what the contract
     *                        says even without a status line)
     */
    static SessionOutcome classify(int exitCode, Optional<LaunchStatus> last, boolean statusRequested,
                                   boolean contractKnown) {
        if (exitCode == 0) {
            return last.isEmpty() && statusRequested ? new NoReadySignal() : new Normal();
        }
        if (last.isPresent() && last.get().state() == LaunchStatus.State.FAILED) {
            LaunchStatus failed = last.get();
            if (isContractCode(failed.code())) {
                return new Refused(failed.code(), Optional.of(failed.message()));
            }
            // A crash (an exception code), including a Debug build's abort that exits with 3.
            return new Crashed(failed.code(), failed.message());
        }
        if (exitCode == CONSOLE_CLOSED_CODE) {
            return new ConsoleClosed();
        }
        if (last.isEmpty() && contractKnown && isContractCode(exitCode)) {
            return new Refused(exitCode, Optional.empty());
        }
        return new Died(exitCode, last.map(LaunchStatus::state));
    }

    private static boolean isContractCode(int code) {
        return code >= EditorExitCode.BAD_ARGUMENTS.code() && code <= EditorExitCode.PROJECT_ALREADY_OPEN.code();
    }

    /** Whether the failure dialog explains this end (the others are a banner, or nothing). */
    default boolean showsFailureDialog() {
        return switch (this) {
            case Refused refused -> refused.code() != EditorExitCode.PROJECT_ALREADY_OPEN.code();
            case Crashed crashed -> true;
            case Died died -> true;
            case Normal normal -> false;
            case NoReadySignal noSignal -> false;
            case ConsoleClosed closed -> false;
        };
    }

    /** One sentence for the top of the failure dialog or a banner. */
    default String headline(String projectName) {
        String name = "\"" + projectName + "\"";
        return switch (this) {
            case Normal normal -> "The editor of " + name + " closed.";
            case NoReadySignal noSignal -> "The editor of " + name + " closed without ever reporting that it was ready"
                    + " (it may be an editor that does not report its state). Check its log.";
            case ConsoleClosed closed -> "The editor of " + name + " was closed from its console window; anything it"
                    + " had not saved is lost.";
            case Refused refused -> refused.code() == EditorExitCode.PROJECT_ALREADY_OPEN.code()
                    ? name + " is already open in another editor" + refused.message().map(m -> ": " + m).orElse(".")
                    : "The editor could not open " + name + ".";
            case Crashed crashed -> "The editor crashed while " + name + " was open.";
            case Died died -> "The editor of " + name + " stopped unexpectedly.";
        };
    }

    /** What happened, in a few lines, for the failure dialog. */
    default String details() {
        return switch (this) {
            case Normal normal -> "It exited with code 0.";
            case NoReadySignal noSignal -> "It exited with code 0 and wrote no launch status.";
            case ConsoleClosed closed -> "It exited with " + EditorExitCode.describe(CONSOLE_CLOSED_CODE) + ".";
            case Refused refused -> {
                String text = "It exited with " + EditorExitCode.describe(refused.code()) + "."
                        + refused.message().map(m -> "\nThe editor said: " + m).orElse("");
                if (refused.code() == EditorExitCode.BAD_ARGUMENTS.code()
                        && refused.message().map(m -> m.contains("--console")).orElse(false)) {
                    text += "\nThis editor does not know --console: turn off \"Show editor console\" in Settings, or"
                            + " rebuild the editor.";
                }
                yield text;
            }
            case Crashed crashed -> "It crashed with " + EditorExitCode.describe(crashed.code()) + "."
                    + "\nThe editor said: " + crashed.message();
            case Died died -> "It exited with " + EditorExitCode.describe(died.exitCode())
                    + died.lastState().map(state -> switch (state) {
                        case LOADING -> " while it was still loading";
                        case READY -> " after it was ready";
                        case FAILED -> "";
                    }).orElse("") + ", without saying why (it may have been killed, or it stopped on an assertion).";
        };
    }
}
