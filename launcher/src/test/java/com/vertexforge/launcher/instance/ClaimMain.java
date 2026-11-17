package com.vertexforge.launcher.instance;

import com.vertexforge.launcher.instance.InstanceRequest.Action;
import com.vertexforge.launcher.instance.SingleInstance.Claim;
import com.vertexforge.launcher.instance.SingleInstance.Forwarded;
import com.vertexforge.launcher.instance.SingleInstance.Independent;
import com.vertexforge.launcher.instance.SingleInstance.Options;
import com.vertexforge.launcher.instance.SingleInstance.Primary;

import java.nio.file.Path;
import java.util.List;
import java.util.concurrent.TimeUnit;

/**
 * A second launcher process for {@code SingleInstanceChildProcessTest} (VK-1685): it claims the single instance with
 * the real sockets and the real foreground grant, as {@code Launcher.main} does.
 * <p>
 * Arguments: {@code <settings folder> <base name> <project> <deadline ms>}. Prints {@code FORWARDED <ms>} and exits 0
 * when the running launcher took its request; exits 2 when it became the running launcher, 3 when it would open its
 * own window, 1 on any failure (with the stack trace).
 */
public final class ClaimMain {

    public static final String FORWARDED = "FORWARDED";

    private ClaimMain() {
    }

    public static void main(String[] args) {
        try {
            long start = System.nanoTime();
            Path configDir = Path.of(args[0]);
            String baseName = args[1];
            Path project = Path.of(args[2]);
            long deadline = start + TimeUnit.MILLISECONDS.toNanos(Long.parseLong(args[3]));
            InstanceMessage message = new InstanceMessage(List.of(new InstanceRequest(Action.OPEN, project)),
                    List.of());

            Claim claim = SingleInstance.claim(configDir, baseName, message, deadline,
                    Options.system(System.err::println));
            long elapsed = TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - start);

            switch (claim) {
                case Forwarded forwarded -> {
                    System.out.println(FORWARDED + " " + elapsed);
                    System.out.flush();
                    System.exit(0);
                }
                case Primary primary -> {
                    System.out.println("PRIMARY");
                    primary.release();
                    System.exit(2);
                }
                case Independent independent -> {
                    System.out.println("INDEPENDENT " + independent.reason());
                    System.exit(3);
                }
            }
        } catch (Throwable t) {
            t.printStackTrace();
            System.exit(1);
        }
    }
}
