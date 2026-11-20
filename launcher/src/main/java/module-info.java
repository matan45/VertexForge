/**
 * The VertexForge launcher (VK-1692). The app image runs it as a named module on a jlink runtime that holds the JDK
 * modules, the JavaFX jmods and Gson; the unit tests run the same classes from the classpath.
 *
 * <p>Every {@code requires} here is also in the image profile's jlink {@code --add-modules} (pom.xml) and in the
 * runtime module list launcher.yml checks; LauncherModuleTest pins all three. Open only what reflection needs:
 * <ul>
 *   <li>javafx.graphics constructs {@link com.vertexforge.launcher.LauncherApp} in {@code Application.launch}, which
 *       needs the package exported to it;</li>
 *   <li>FXMLLoader injects MainController's private {@code @FXML} fields and calls its {@code #handlers}, which
 *       needs the package open to it.</li>
 * </ul>
 * Gson is used only through its tree and streaming API (JsonParser, JsonReader, JsonObject), so no package is open to
 * it. Binding a launcher class with {@code Gson.fromJson(json, SomeClass.class)} would need
 * {@code opens <package> to com.google.gson}, and without it fails only in the image.
 */
module com.vertexforge.launcher {
    requires javafx.controls;
    requires javafx.fxml;
    requires com.google.gson;

    exports com.vertexforge.launcher to javafx.graphics;
    opens com.vertexforge.launcher.controller to javafx.fxml;
}
