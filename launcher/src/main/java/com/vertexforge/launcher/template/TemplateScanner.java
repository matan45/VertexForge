package com.vertexforge.launcher.template;

import com.vertexforge.launcher.engine.EngineContent;

import java.io.IOException;

/**
 * Reads what an engine offers for new projects (VK-1689). Blocks on the file system, so it only runs on a background
 * thread; the engine's folders may be on a slow network share.
 */
@FunctionalInterface
public interface TemplateScanner {

    /** Reads the engine's folders from disk. */
    TemplateScanner SYSTEM = FolderTemplateScanner::scan;

    /**
     * @throws IOException when the script library or the templates folder cannot be read at all; a template or an
     *                     option that cannot be used is left out of the catalog instead, with the reason
     */
    TemplateCatalog scan(EngineContent engine) throws IOException;
}
