package com.vertexforge.launcher.service;

import com.vertexforge.launcher.TestSupport;
import com.vertexforge.launcher.engine.EngineHealth;
import com.vertexforge.launcher.engine.EngineManifest;
import com.vertexforge.launcher.engine.EngineOption;
import com.vertexforge.launcher.engine.EngineOptions;
import com.vertexforge.launcher.engine.EngineRegistryView;
import com.vertexforge.launcher.env.DriveTypes;
import com.vertexforge.launcher.env.LauncherEnvironment;
import com.vertexforge.launcher.env.RecycleBin;
import com.vertexforge.launcher.store.StoreStatus;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;
import org.junit.jupiter.api.io.TempDir;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Clock;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** Where "Move to Recycle Bin…" refuses to go (VK-1695), checked on paths alone (Windows paths). */
@EnabledOnOs(OS.WINDOWS)
class DeleteGuardsTest {

    private static final DriveTypes FIXED = root -> DriveTypes.Kind.LOCAL;

    @Test
    void aNetworkShareIsRefusedWithoutAskingForItsDriveType() {
        DriveTypes never = root -> {
            throw new AssertionError("a share needs no drive type");
        };

        Optional<String> reason = DeleteGuards.driveRefusal(Path.of("\\\\server\\share\\Game"), never);

        assertTrue(reason.orElseThrow().contains("network share"), reason::toString);
    }

    @Test
    void aFixedDrivePasses() {
        assertEquals(Optional.empty(), DeleteGuards.driveRefusal(Path.of("C:\\projects\\Game"), FIXED));
    }

    @Test
    void theEnginesFoldersAreItsInstallItsWorkingDirectoryAndItsContent() throws Exception {
        Path exe = Path.of("C:\\engines\\vf\\bin\\Editor\\Release\\x64\\Editor.exe");
        EngineManifest manifest = EngineManifest.parse("{\"manifestVersion\": 1, \"engineVersion\": \"1.0.0\","
                + " \"configuration\": \"Release\", \"editorExe\": \"Editor.exe\", \"workingDirectory\": \".\","
                + " \"templatesDir\": \"../../../../assets/templates\"}");
        EngineOption engine = new EngineOption(Optional.of(UUID.randomUUID()), exe, exe.toString(),
                EngineHealth.ok(manifest, exe.getParent()), false, false, false);
        EngineOption broken = new EngineOption(Optional.empty(), null, "not a path", EngineHealth.broken("gone"),
                false, false, true);
        EngineOptions options = new EngineOptions(new EngineRegistryView(List.of(), Optional.empty(), StoreStatus.OK,
                Optional.empty(), "", 0), Optional.of(broken), List.of(engine));

        List<Path> folders = DeleteGuards.engineFolders(options);

        assertEquals(List.of(exe.getParent(), exe.getParent(), Path.of("C:\\engines\\vf\\assets\\templates")),
                folders);
    }

    @Test
    void theProtectedFoldersComeFromTheEnvironment() {
        Path config = Path.of("C:\\Users\\someone\\.vertexforge");
        Map<String, String> variables = Map.of("TEMP", "C:\\Users\\someone\\AppData\\Local\\Temp",
                "OneDrive", "C:\\Users\\someone\\OneDrive", "PUBLIC", "relative\\ignored");
        LauncherEnvironment environment = new LauncherEnvironment(Clock.systemUTC(), config,
                Optional.of(Path.of("C:\\Program Files\\VertexForge\\VertexForge Launcher.exe")), variables::get,
                builder -> {
                    throw new AssertionError("nothing is started");
                }, StandardCharsets.UTF_8, DriveTypes.unknown(), RecycleBin.unavailable(),
                () -> List.of(Path.of("D:\\Moved\\Documents")));

        ProtectedFolders folders = ProtectedFolders.of(environment);

        assertTrue(folders.neverThisOrAbove().containsAll(List.of(config, Path.of("C:\\Users\\someone"),
                Path.of("C:\\Users\\someone\\Desktop"), Path.of("C:\\Users\\someone\\Documents"),
                Path.of("C:\\Users\\someone\\Downloads"), Path.of("D:\\Moved\\Documents"),
                Path.of("C:\\Users\\someone\\AppData\\Local\\Temp"), Path.of("C:\\Users\\someone\\OneDrive"),
                Path.of("C:\\Program Files\\VertexForge"))), folders.neverThisOrAbove()::toString);
        assertTrue(folders.neverThisOrAbove().stream().noneMatch(path -> path.toString().contains("ignored")));
        assertEquals(List.of(config), folders.neverInside());
    }

    /** Review M2: the folder Create Project starts in holds projects; it is added per delete. */
    @Test
    void moreProtectedFoldersCanBeAdded() {
        Path projects = Path.of("C:\\Users\\someone\\Projects");
        ProtectedFolders folders = ProtectedFolders.none().with(List.of(projects, Path.of("relative")));
        DeleteGuards.DeleteContext context = new DeleteGuards.DeleteContext(folders, List.of(), List.of());

        assertEquals(List.of(projects), folders.neverThisOrAbove());
        assertTrue(DeleteGuards.refusal(projects, projects, context, FIXED).isPresent());
        assertEquals(Optional.empty(), DeleteGuards.refusal(projects.resolve("Game"), projects.resolve("Game"),
                context, FIXED));
    }

    @Test
    void aProjectInsideATempFolderMayGoButTheTempFolderItselfMayNot() {
        Path temp = Path.of("C:\\Users\\someone\\AppData\\Local\\Temp");
        DeleteGuards.DeleteContext context = new DeleteGuards.DeleteContext(new ProtectedFolders(List.of(temp),
                List.of()), List.of(), List.of());

        assertEquals(Optional.empty(), DeleteGuards.refusal(temp.resolve("Game"), temp.resolve("Game"), context,
                FIXED));
        assertTrue(DeleteGuards.refusal(temp, temp, context, FIXED).isPresent());
        assertTrue(DeleteGuards.refusal(temp.getParent(), temp.getParent(), context, FIXED).isPresent(),
                "a folder that holds a protected one");
    }

    @Test
    void theRealPathCountsTooSoAnotherSpellingDoesNotSlipThrough() {
        Path documents = Path.of("C:\\Users\\someone\\Documents");
        DeleteGuards.DeleteContext context = new DeleteGuards.DeleteContext(new ProtectedFolders(List.of(documents),
                List.of()), List.of(), List.of());

        // Listed through a link elsewhere, really the Documents folder (a junction, a subst drive).
        assertTrue(DeleteGuards.refusal(Path.of("X:\\Game"), documents, context, FIXED).isPresent());
        // Case is ignored, as Windows ignores it.
        assertTrue(DeleteGuards.refusal(Path.of("C:\\USERS\\SOMEONE\\documents"),
                Path.of("C:\\USERS\\SOMEONE\\documents"), context, FIXED).isPresent());
    }

    @Test
    void theHomeOfTheSettingsFolderIsNeverTaken(@TempDir Path tempDir) throws Exception {
        Path home = Files.createDirectories(TestSupport.real(tempDir).resolve("home"));
        ProtectedFolders folders = ProtectedFolders.of(TestSupport.environment(home.resolve(".vertexforge"),
                Clock.systemUTC(), Map.of(), builder -> {
                    throw new AssertionError("nothing is started");
                }));
        DeleteGuards.DeleteContext context = new DeleteGuards.DeleteContext(folders, List.of(), List.of());

        assertTrue(DeleteGuards.refusal(home, home, context, FIXED).isPresent());
        Path project = Files.createDirectories(home.resolve("Projects/Game"));
        assertEquals(Optional.empty(), DeleteGuards.refusal(project, project, context, FIXED));
    }
}
