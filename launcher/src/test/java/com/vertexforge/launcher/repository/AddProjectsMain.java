package com.vertexforge.launcher.repository;

import com.vertexforge.launcher.util.LauncherLog;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.time.Clock;
import java.util.UUID;

/**
 * A second launcher process for {@code RecentProjectsConcurrencyTest} (VK-1678): it adds projects to a shared store.
 * <p>
 * Arguments: {@code <store file> <projects folder> <name prefix> <count> <limit>}. It prints {@code READY}, waits for
 * one line on stdin (the start signal, so that both processes really overlap), adds {@code count} projects named
 * {@code <prefix>-<i>} and prints {@code DONE}. Exit code 0 on success, 1 on any failure (with the stack trace).
 */
public final class AddProjectsMain {

    public static final String READY = "READY";
    public static final String DONE = "DONE";

    private AddProjectsMain() {
    }

    public static void main(String[] args) {
        try {
            Path storeFile = Path.of(args[0]);
            Path projectsDir = Path.of(args[1]);
            String prefix = args[2];
            int count = Integer.parseInt(args[3]);
            int limit = Integer.parseInt(args[4]);

            Clock clock = Clock.systemUTC();
            LauncherLog log = new LauncherLog(storeFile.resolveSibling("child-" + prefix + ".log"), clock,
                    LauncherLog.DEFAULT_MAX_BYTES, LauncherLog.DEFAULT_BACKUPS, System.err);
            RecentProjectsRepository repository = new RecentProjectsRepository(
                    RecentProjectsRepository.openStore(storeFile, clock, log), UUID::randomUUID, clock, log);

            System.out.println(READY);
            System.out.flush();
            BufferedReader stdin = new BufferedReader(new InputStreamReader(System.in, StandardCharsets.UTF_8));
            if (stdin.readLine() == null) {
                throw new IllegalStateException("no start signal");
            }

            for (int i = 0; i < count; i++) {
                String name = prefix + "-" + i;
                repository.add(projectsDir.resolve(name).resolve(name + ".vfproj"), name, limit);
            }
            System.out.println(DONE);
            System.out.flush();
            System.exit(0);
        } catch (Throwable t) {
            t.printStackTrace();
            System.exit(1);
        }
    }
}
