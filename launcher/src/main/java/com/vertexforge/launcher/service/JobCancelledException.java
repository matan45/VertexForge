package com.vertexforge.launcher.service;

import java.io.IOException;

/**
 * Thrown from inside a file job when the user cancels it (VK-1690). An {@link IOException}, so that it leaves the
 * job's copy loops at once, through the same cleanup as any other failure.
 */
public final class JobCancelledException extends IOException {

    public JobCancelledException() {
        super("The job was cancelled");
    }
}
