package com.vertexforge.launcher.session;

import java.time.Duration;
import java.util.PriorityQueue;

/**
 * A {@link SessionScheduler} on a virtual clock: nothing runs until the test calls {@link #runDue()} or
 * {@link #advance(Duration)}, and then everything runs on the test's thread, in due order (ties in the order they
 * were scheduled).
 */
public final class ManualScheduler implements SessionScheduler {

    private static final class Task implements Comparable<Task> {
        final long due;
        final long sequence;
        final Runnable action;
        boolean cancelled;

        Task(long due, long sequence, Runnable action) {
            this.due = due;
            this.sequence = sequence;
            this.action = action;
        }

        @Override
        public int compareTo(Task other) {
            int byDue = Long.compare(due, other.due);
            return byDue != 0 ? byDue : Long.compare(sequence, other.sequence);
        }
    }

    private final PriorityQueue<Task> queue = new PriorityQueue<>();
    private long now;
    private long sequence;
    private boolean closed;

    @Override
    public synchronized Cancellable schedule(Duration delay, Runnable task) {
        if (closed) {
            return () -> {
            };
        }
        Task scheduled = new Task(now + delay.toNanos(), sequence++, task);
        queue.add(scheduled);
        return () -> scheduled.cancelled = true;
    }

    @Override
    public void execute(Runnable task) {
        schedule(Duration.ZERO, task);
    }

    @Override
    public synchronized void close() {
        closed = true;
        queue.clear();
    }

    /** Runs every task that is due now, including ones they schedule for now. */
    public void runDue() {
        advance(Duration.ZERO);
    }

    /** Moves the clock forward by {@code duration}, running every task that falls due on the way, in order. */
    public void advance(Duration duration) {
        long target;
        synchronized (this) {
            target = now + duration.toNanos();
        }
        while (true) {
            Task next;
            synchronized (this) {
                next = queue.peek();
                if (next == null || next.due > target) {
                    now = target;
                    return;
                }
                queue.poll();
                now = next.due;
            }
            if (!next.cancelled) {
                next.action.run();
            }
        }
    }

    /** How many tasks are waiting (cancelled ones included). */
    public synchronized int pending() {
        return queue.size();
    }
}
