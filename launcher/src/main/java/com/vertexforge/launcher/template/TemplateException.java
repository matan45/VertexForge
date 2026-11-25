package com.vertexforge.launcher.template;

/**
 * A project template that cannot be used, or a choice of template and options that cannot be built (VK-1689). The
 * message is written for the user: the dialog shows it as it is.
 */
public class TemplateException extends Exception {

    public TemplateException(String message) {
        super(message);
    }
}
