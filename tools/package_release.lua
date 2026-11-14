-- premake5 package-release
-- Assembles a single, ready-to-ship VertexForge engine/editor distribution into
-- dist/VertexForge/. This is the *engine* package (Editor + Runtime + SDK +
-- resources + Tests + Java launcher) — NOT a game export (that's GameExport →
-- .vfpak, which packages an individual game).
--
-- Run order (the user builds Release themselves first):
--   msbuild VFEngine/VertexForge.sln /p:Configuration=Release /p:Platform=x64
--   premake5 package-release
--
-- Bundle layout (the bin/ tree is preserved so existing relative paths resolve):
--   dist/VertexForge/
--     VertexForge Launcher/           jpackage APP_IMAGE (bundled JRE; sibling of bin/) — entry point
--     bin/Editor/Release/x64/         Editor.exe + all DLLs + engine.json (VK-1680 manifest)
--     bin/Editor/resources/           editor/, shaders/, ibl/   (../../resources/ target for Editor.exe)
--     bin/Runtime/Release/x64/        Runtime.exe + all DLLs
--     bin/Runtime/resources/          editor/, shaders/, ibl/   (../../resources/ target for Runtime.exe)
--     bin/Tests/Release/x64/          Tests.exe + DLLs
--     sdk/                            plugin headers + deps + lib/Release/imgui.lib + template
--
-- Editor/Runtime resolve engine assets via "../../resources/" relative to the exe
-- (cwd = the x64/ folder), so resources live at bin/<App>/resources/, two levels
-- up from each exe — NOT at the bundle root. The launcher app-image sits beside
-- bin/: the launcher finds its bundled engine from its own exe (jpackage.app-path)
-- as ../bin/Editor/Release/x64/Editor.exe (BundledEngine.java, VK-1681), whatever
-- folder it is started from, and registers it in ~/.vertexforge/engines.json.
--
-- engine.json (tools/engine_version.lua) records workingDirectory "." (the exe
-- folder) and the engine content paths assets/templates and assets/scripts/lib at
-- the bundle root, mirroring the repo layout; VK-1688 copies those folders there.

function vfPackageRelease()
   local dist = "dist/VertexForge"

   -- Recursively copy every file under srcBase into dstBase, preserving the tree.
   -- (Same helper shape as tools/export_sdk.lua's copyTree.)
   local function copyTree(srcBase, pattern, dstBase)
      local files = os.matchfiles(path.join(srcBase, pattern))
      for _, f in ipairs(files) do
         local rel = path.getrelative(srcBase, f)
         local d = path.join(dstBase, rel)
         os.mkdir(path.getdirectory(d))
         os.copyfile(f, d)
      end
      return #files
   end

   -- 1. Preflight: Release binaries must already be built.
   local required = {
      "bin/Editor/Release/x64/Editor.exe",
      "bin/Runtime/Release/x64/Runtime.exe",
      "bin/Tests/Release/x64/Tests.exe",
   }
   for _, exe in ipairs(required) do
      if not os.isfile(exe) then
         print("ERROR: missing " .. exe)
         print("Build Release first:")
         print("  msbuild VFEngine/VertexForge.sln /p:Configuration=Release /p:Platform=x64")
         return
      end
   end

   -- 1b. Preflight: the Release Editor's own manifest (its postbuild writes it) must
   --     match VERSION, or the bundle would claim a version its binaries were not built as.
   local buildManifestPath = "bin/Editor/Release/x64/engine.json"
   local buildManifest = nil
   if os.isfile(buildManifestPath) then
      local ok, decoded = pcall(json.decode, io.readfile(buildManifestPath))
      if ok and type(decoded) == "table" then
         buildManifest = decoded
      end
   end
   local engineVersion = vfReadEngineVersion().text
   if buildManifest and buildManifest.engineVersion ~= engineVersion then
      print("ERROR: the Release binaries were built as engine " .. tostring(buildManifest.engineVersion) ..
         " but VERSION is " .. engineVersion .. ".")
      print("Re-run premake5 vs2022 and rebuild Release.")
      return
   end

   print("Packaging VertexForge release into " .. dist .. "/ ...")

   -- 2. Clean slate.
   os.rmdir(dist)
   os.mkdir(dist)

   -- 3. Binaries (exe + every DLL beside it: subsystems, OpenAL, streamline, assimp).
   local n
   n = copyTree("bin/Editor/Release/x64",  "**", dist .. "/bin/Editor/Release/x64")
   print("  Editor binaries: " .. n)
   n = copyTree("bin/Runtime/Release/x64", "**", dist .. "/bin/Runtime/Release/x64")
   print("  Runtime binaries: " .. n)
   n = copyTree("bin/Tests/Release/x64",   "**", dist .. "/bin/Tests/Release/x64")
   print("  Tests binaries: " .. n)

   -- 3b. engine.json (VK-1680). Step 3 copied the dev-tree manifest; rewrite it for the
   --     bundle: working directory = the exe folder, content paths under the bundle root.
   --     gitCommit = the commit the Release Editor was built at, when its manifest has one.
   if not buildManifest then
      print("  WARNING: " .. buildManifestPath .. " missing or unreadable; gitCommit = current HEAD")
   end
   local releaseExeDir = path.getabsolute(dist .. "/bin/Editor/Release/x64")
   vfWriteEngineManifest {
      exeDir = releaseExeDir,
      workingDir = releaseExeDir,
      contentRoot = path.getabsolute(dist),
      configuration = "Release",
      gitCommit = (buildManifest and type(buildManifest.gitCommit) == "string") and buildManifest.gitCommit
         or vfGitCommit()
   }

   -- 4. Resources — placed two levels up from each exe so "../../resources/" resolves.
   --    Tests is CPU-only and needs none.
   n = copyTree("resources", "**", dist .. "/bin/Editor/resources")
   copyTree("resources", "**", dist .. "/bin/Runtime/resources")
   print("  resources (x2): " .. n .. " files each")

   -- 4b. Splash logo — SplashScreen::loadSplashImage() searches exe-relative paths
   --     (4-up to the dev-tree resources/, then beside the exe), NOT the engine's
   --     cwd-relative "../../resources/". Drop the PNG beside Editor.exe so the
   --     beside-the-exe fallback resolves in the packaged layout.
   os.copyfile("resources/editor/VertexForge-logo.png",
               dist .. "/bin/Editor/Release/x64/VertexForge-logo.png")
   print("  splash logo: beside Editor.exe")

   -- 5. SDK — refresh lib/Release/imgui.lib from this Release build, then copy.
   if vfExportPluginSDK then
      vfExportPluginSDK()
   else
      print("  WARNING: vfExportPluginSDK not in scope; copying existing sdk/ as-is")
   end
   n = copyTree("sdk", "**", dist .. "/sdk")
   print("  sdk: " .. n .. " files")

   -- 6. Java launcher → self-contained jpackage app-image (bundled JRE).
   --    Needs JDK 25 on PATH or in JAVA_HOME; the Maven wrapper (launcher/mvnw.cmd) downloads the
   --    pinned Maven on first use. `package` also runs the launcher's unit tests (VK-1677), so a red
   --    test means no app image. The panteleyev plugin goal is jpackage:jpackage.
   print("  Building launcher (mvnw clean package jpackage:jpackage) ...")
   local ok = os.execute('cd launcher && mvnw.cmd -B clean package jpackage:jpackage')
   local appImage = "launcher/target/dist/VertexForge Launcher"
   if ok and os.isdir(appImage) then
      n = copyTree(appImage, "**", dist .. "/VertexForge Launcher")
      print("  launcher app-image: " .. n .. " files")
   else
      print("  WARNING: launcher app-image not produced (is JDK 25 on PATH or JAVA_HOME? did a launcher test fail?).")
      print("           Native bundle is complete; re-run after building " .. appImage .. "/.")
   end

   print("Done. Release package at " .. path.getabsolute(dist))
   print("Run: \"" .. dist .. "/VertexForge Launcher/VertexForge Launcher.exe\"")
end

newaction {
   trigger = "package-release",
   description = "Assemble the single-folder ship distribution (Editor+Runtime+SDK+resources+Tests+launcher) into dist/VertexForge/",
   execute = vfPackageRelease
}
