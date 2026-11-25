package com.vertexforge.launcher.store;

/** A document that parses as JSON but does not have the shape its {@link DocumentFormat} requires. */
public final class CorruptDocumentException extends Exception {

    public CorruptDocumentException(String message) {
        super(message);
    }
}
