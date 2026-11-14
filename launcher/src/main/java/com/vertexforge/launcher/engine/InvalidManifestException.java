package com.vertexforge.launcher.engine;

/** An {@code engine.json} that this launcher cannot use (VK-1681). The message says why, for the user. */
public final class InvalidManifestException extends Exception {

    public InvalidManifestException(String message) {
        super(message);
    }
}
