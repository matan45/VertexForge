package com.vertexforge.launcher.instance;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.stream.IntStream;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The running launcher's request inbox (VK-1685). */
class InstanceInboxTest {

    private static InstanceMessage message(int number) {
        return new InstanceMessage(List.of(), List.of(Integer.toString(number)));
    }

    private static int number(InstanceMessage message) {
        return Integer.parseInt(message.problems().getFirst());
    }

    @Test
    void messagesOfferedBeforeTheWindowAttachesAreDeliveredInOrderOnAttach() {
        InstanceInbox inbox = new InstanceInbox();
        assertTrue(inbox.offer(message(1)));
        assertTrue(inbox.offer(message(2)));
        List<Integer> delivered = new ArrayList<>();
        List<Thread> threads = new ArrayList<>();

        inbox.attach(received -> {
            delivered.add(number(received));
            threads.add(Thread.currentThread());
        });

        assertEquals(List.of(1, 2), delivered);
        assertTrue(threads.stream().allMatch(thread -> thread == Thread.currentThread()));
    }

    @Test
    void afterAttachEachMessageGoesStraightToTheSinkOnTheOfferingThread() throws Exception {
        InstanceInbox inbox = new InstanceInbox();
        List<Thread> threads = Collections.synchronizedList(new ArrayList<>());
        inbox.attach(received -> threads.add(Thread.currentThread()));

        Thread offering = new Thread(() -> inbox.offer(message(1)), "offering");
        offering.start();
        offering.join(5000);

        assertEquals(1, threads.size());
        assertSame(offering, threads.getFirst());
    }

    @Test
    void aSecondAttachIsAnError() {
        InstanceInbox inbox = new InstanceInbox();
        inbox.attach(received -> {
        });

        assertThrows(IllegalStateException.class, () -> inbox.attach(received -> {
        }));
    }

    @Test
    void aClosingInboxTakesNothingMoreButKeepsWhatItHas() {
        InstanceInbox inbox = new InstanceInbox();
        inbox.offer(message(1));
        inbox.markClosing();
        List<Integer> delivered = new ArrayList<>();

        assertTrue(inbox.isClosing());
        assertFalse(inbox.offer(message(2)));
        inbox.attach(received -> delivered.add(number(received)));
        assertFalse(inbox.offer(message(3)));

        assertEquals(List.of(1), delivered);
    }

    @Test
    void aSinkThatThrowsMeansTheMessageWasNotTaken() {
        InstanceInbox inbox = new InstanceInbox();
        inbox.attach(received -> {
            throw new IllegalStateException("the toolkit has exited");
        });

        assertFalse(inbox.offer(message(1)));
    }

    @Test
    @Timeout(value = 30, unit = TimeUnit.SECONDS)
    void attachingWhileMessagesArriveLosesNoneAndKeepsTheOrder() throws Exception {
        InstanceInbox inbox = new InstanceInbox();
        int count = 2000;
        CountDownLatch someOffered = new CountDownLatch(100);
        Thread producer = new Thread(() -> {
            for (int i = 0; i < count; i++) {
                inbox.offer(message(i));
                someOffered.countDown();
            }
        }, "producer");
        List<Integer> delivered = Collections.synchronizedList(new ArrayList<>());

        producer.start();
        assertTrue(someOffered.await(10, TimeUnit.SECONDS));
        inbox.attach(received -> delivered.add(number(received)));
        producer.join(20_000);

        assertEquals(IntStream.range(0, count).boxed().toList(), delivered);
    }
}
