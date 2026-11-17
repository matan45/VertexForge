package com.vertexforge.launcher.instance;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.vertexforge.launcher.instance.InstanceMessages.Ack;
import com.vertexforge.launcher.instance.InstanceMessages.InvalidMessageException;
import com.vertexforge.launcher.instance.InstanceRequest.Action;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** The single-instance wire messages (VK-1685). */
class InstanceMessagesTest {

    @TempDir
    Path tempDir;

    @Test
    void theGreetingCarriesThePidOnOneLine() throws Exception {
        String line = InstanceMessages.greeting(1234);

        assertEquals("{\"app\":\"vertexforge-launcher\",\"v\":1,\"pid\":1234}", line);
        assertEquals(1234, InstanceMessages.parseGreeting(line));
    }

    @Test
    void aGreetingFromAnythingElseIsRefused() {
        assertRefused(() -> InstanceMessages.parseGreeting("{\"app\":\"other\",\"v\":1,\"pid\":1}"));
        assertRefused(() -> InstanceMessages.parseGreeting("{\"app\":\"vertexforge-launcher\",\"v\":2,\"pid\":1}"));
        assertRefused(() -> InstanceMessages.parseGreeting("{\"app\":\"vertexforge-launcher\",\"v\":1}"));
        assertRefused(() -> InstanceMessages.parseGreeting("{\"app\":\"vertexforge-launcher\",\"v\":1,\"pid\":0}"));
        assertRefused(() -> InstanceMessages.parseGreeting("{\"app\":\"vertexforge-launcher\",\"v\":1.5,\"pid\":1}"));
        assertRefused(() -> InstanceMessages.parseGreeting("hello"));
        assertRefused(() -> InstanceMessages.parseGreeting(""));
        assertRefused(() -> InstanceMessages.parseGreeting(null));
    }

    @Test
    void aRequestRoundTripsUnicodeQuotesAndBackslashesOnOneLine() throws Exception {
        Path hebrew = tempDir.resolve("פרויקט 🎮").resolve("משחק.vfproj");
        Path plain = tempDir.resolve("back\\slash").resolve("Game.vfproj");
        InstanceMessage message = new InstanceMessage(
                List.of(new InstanceRequest(Action.OPEN, hebrew), new InstanceRequest(Action.ADD, plain)),
                List.of("Unknown option --\"quoted\"", "line\nbreak \\ and   separator"));

        String line = InstanceMessages.request(message);

        assertFalse(line.contains("\n"), line);
        assertFalse(line.contains("\r"), line);
        assertFalse(line.contains(" "), line);
        assertEquals(message, InstanceMessages.parseRequest(line));
    }

    @Test
    void anEmptyRequestMeansComeToTheFront() throws Exception {
        assertEquals(InstanceMessage.EMPTY, InstanceMessages.parseRequest(InstanceMessages.request(InstanceMessage.EMPTY)));
        // "problems" is optional.
        assertEquals(InstanceMessage.EMPTY, InstanceMessages.parseRequest("{\"v\":1,\"requests\":[]}"));
    }

    @Test
    void unknownMembersAreIgnored() throws Exception {
        Path project = tempDir.resolve("Game.vfproj");
        JsonObject item = new JsonObject();
        item.addProperty("action", "add");
        item.addProperty("path", project.toString());
        item.addProperty("future", 7);
        JsonArray requests = new JsonArray();
        requests.add(item);
        JsonObject root = new JsonObject();
        root.addProperty("v", 1);
        root.add("requests", requests);
        root.addProperty("sentAt", "later");

        assertEquals(new InstanceMessage(List.of(new InstanceRequest(Action.ADD, project)), List.of()),
                InstanceMessages.parseRequest(root.toString()));
    }

    @Test
    void badRequestsAreRefused() {
        String absolute = tempDir.resolve("Game.vfproj").toString();

        assertRefused(() -> InstanceMessages.parseRequest(request(2, "open", absolute)));
        assertRefused(() -> InstanceMessages.parseRequest(request(1, "delete", absolute)));
        assertRefused(() -> InstanceMessages.parseRequest(request(1, null, absolute)));
        assertRefused(() -> InstanceMessages.parseRequest(request(1, "open", "relative\\Game.vfproj")));
        assertRefused(() -> InstanceMessages.parseRequest(request(1, "open", null)));
        assertRefused(() -> InstanceMessages.parseRequest("{\"v\":1,\"requests\":{}}"));
        assertRefused(() -> InstanceMessages.parseRequest("{\"v\":1}"));
        assertRefused(() -> InstanceMessages.parseRequest("{\"v\":1,\"requests\":[1]}"));
        assertRefused(() -> InstanceMessages.parseRequest("{\"v\":1,\"requests\":[],\"problems\":[3]}"));
        assertRefused(() -> InstanceMessages.parseRequest("{\"v\":1,\"requests\":[],\"problems\":\"x\"}"));
        assertRefused(() -> InstanceMessages.parseRequest("{\"v\":1,\"requests\":[]} {}"));
        assertRefused(() -> InstanceMessages.parseRequest("{\"v\":1,\"requests\":[],}"));
        assertRefused(() -> InstanceMessages.parseRequest("[]"));
    }

    @Test
    void aRequestOverTheLimitIsRefused() {
        InstanceMessage huge = new InstanceMessage(List.of(),
                List.of("x".repeat(InstanceMessages.MAX_LINE_BYTES)));

        InvalidMessageException refused = assertThrows(InvalidMessageException.class,
                () -> InstanceMessages.parseRequest(InstanceMessages.request(huge)));
        assertTrue(refused.getMessage().contains("longer than"), refused.getMessage());
    }

    @Test
    void answersRoundTrip() throws Exception {
        assertEquals(new Ack(true, null), InstanceMessages.parseAck(InstanceMessages.ack()));
        assertEquals(new Ack(false, "closing"), InstanceMessages.parseAck(InstanceMessages.refusal("closing")));
        assertRefused(() -> InstanceMessages.parseAck("{\"error\":\"x\"}"));
        assertRefused(() -> InstanceMessages.parseAck("{\"ok\":\"yes\"}"));
    }

    @Test
    void invalidJsonSaysWhyWithoutGsonsHelpLink() {
        InvalidMessageException refused = assertThrows(InvalidMessageException.class,
                () -> InstanceMessages.parseRequest("{\"v\":1,"));

        assertTrue(refused.getMessage().startsWith("the message is not valid JSON: "), refused.getMessage());
        assertFalse(refused.getMessage().contains("https://"), refused.getMessage());
        assertFalse(refused.getMessage().contains("\n"), refused.getMessage());
    }

    private static String request(int version, String action, String path) {
        JsonObject item = new JsonObject();
        if (action != null) {
            item.addProperty("action", action);
        }
        if (path != null) {
            item.addProperty("path", path);
        }
        JsonArray requests = new JsonArray();
        requests.add(item);
        JsonObject root = new JsonObject();
        root.addProperty("v", version);
        root.add("requests", requests);
        return root.toString();
    }

    private static void assertRefused(org.junit.jupiter.api.function.Executable parse) {
        InvalidMessageException refused = assertThrows(InvalidMessageException.class, parse);
        assertFalse(refused.getMessage() == null || refused.getMessage().isBlank());
    }

    @Test
    void anAckWithoutAnErrorHasNone() throws Exception {
        assertNull(InstanceMessages.parseAck("{\"ok\":false}").error());
    }
}
