package com.vertexforge.launcher.instance;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.function.Consumer;

/**
 * Where the running launcher's requests wait for its window (VK-1685): its own command line, and every message a
 * second launcher forwards. Until the window {@linkplain #attach attaches}, messages are kept; after that each one is
 * handed straight to the window's sink. Order is kept throughout. Toolkit-free and thread-safe.
 * <p>
 * Once {@linkplain #markClosing closing}, nothing more is taken, so the single-instance server refuses a second
 * launcher's request instead of acknowledging one that would be lost; that launcher then waits for this one to exit
 * or opens its own window.
 */
public final class InstanceInbox {

    private final Object guard = new Object();
    private final List<InstanceMessage> waiting = new ArrayList<>();
    private Consumer<InstanceMessage> sink;
    private volatile boolean closing;

    /**
     * Takes a message: kept until {@link #attach} when no sink is attached yet, else handed to the sink on the calling
     * thread. Messages are handed over one at a time, in the order they were offered.
     *
     * @return {@code false} when the inbox is closing or the sink threw (the message was not taken)
     */
    public boolean offer(InstanceMessage message) {
        Objects.requireNonNull(message, "message");
        synchronized (guard) {
            if (closing) {
                return false;
            }
            if (sink == null) {
                waiting.add(message);
                return true;
            }
            try {
                sink.accept(message);
                return true;
            } catch (RuntimeException e) {
                return false;
            }
        }
    }

    /**
     * Attaches the window's sink and hands it every kept message, in order, on the calling thread. A sink that throws
     * for a kept message loses only that message.
     *
     * @throws IllegalStateException when a sink is already attached
     */
    public void attach(Consumer<InstanceMessage> newSink) {
        Objects.requireNonNull(newSink, "newSink");
        synchronized (guard) {
            if (sink != null) {
                throw new IllegalStateException("A sink is already attached");
            }
            sink = newSink;
            for (InstanceMessage message : waiting) {
                try {
                    newSink.accept(message);
                } catch (RuntimeException e) {
                    // That message is lost; the others are still delivered.
                }
            }
            waiting.clear();
        }
    }

    /** From now on {@link #offer} takes nothing. Messages already kept stay for {@link #attach}. */
    public void markClosing() {
        synchronized (guard) {
            closing = true;
        }
    }

    public boolean isClosing() {
        return closing;
    }
}
