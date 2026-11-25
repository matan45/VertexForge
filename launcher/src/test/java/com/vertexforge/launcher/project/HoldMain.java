package com.vertexforge.launcher.project;

import java.time.Duration;

/**
 * A stand-in for ANOTHER launcher that is updating a project's script library (VK-1691): it takes
 * {@link EditorLockProbe#hold} on a project folder, as the update does, prints {@link #HOLDING} (or {@link #NOT_HELD}
 * and the state), then keeps the hold until its standard input closes. A second process is needed: Java file locks
 * belong to the whole JVM.
 * <p>
 * Argument: the project folder. Exits 0, or 1 on any failure.
 */
public final class HoldMain {

    public static final String HOLDING = "HOLDING";
    public static final String NOT_HELD = "NOT_HELD";

    private HoldMain() {
    }

    public static void main(String[] args) {
        try {
            EditorLockProbe probe = new EditorLockProbe(0, Duration.ZERO, duration -> {
            });
            EditorLockProbe.HoldResult result = probe.hold(java.nio.file.Path.of(args[0]));
            if (!(result instanceof EditorLockProbe.HoldResult.Holding holding)) {
                System.out.println(NOT_HELD + " " + result);
                System.out.flush();
                System.exit(0);
                return;
            }
            try (EditorLockProbe.Hold ignored = holding.hold()) {
                System.out.println(HOLDING);
                System.out.flush();
                while (System.in.read() >= 0) {
                    // Holds until the test closes this process's standard input.
                }
            }
            System.exit(0);
        } catch (java.io.IOException | RuntimeException e) {
            e.printStackTrace();
            System.exit(1);
        }
    }
}
