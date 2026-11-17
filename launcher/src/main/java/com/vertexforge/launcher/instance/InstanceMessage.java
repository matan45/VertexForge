package com.vertexforge.launcher.instance;

import java.util.List;

/**
 * What one launcher start asks for (VK-1685): its requests, in command-line order, and the problems found on its
 * command line, which the running launcher shows. A message with neither only brings the running launcher to the
 * front.
 *
 * @param requests the projects to add or open, in order
 * @param problems one sentence per argument that could not be used
 */
public record InstanceMessage(List<InstanceRequest> requests, List<String> problems) {

    /** Nothing to do but come to the front. */
    public static final InstanceMessage EMPTY = new InstanceMessage(List.of(), List.of());

    public InstanceMessage {
        requests = List.copyOf(requests);
        problems = List.copyOf(problems);
    }

    /** Whether the message asks for nothing but coming to the front. */
    public boolean isEmpty() {
        return requests.isEmpty() && problems.isEmpty();
    }
}
