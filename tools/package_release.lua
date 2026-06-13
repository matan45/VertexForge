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
--     VertexForge.bat                 entry point (sets VERTEXFORGE_EDITOR_PATH, runs launcher)
--     VertexForge Launcher/           jpackage APP_IMAGE (bundled JRE; sibling of bin/)
--     bin/Editor/Release/x64/         Editor.exe + all DLLs
--     bin/Editor/resources/           editor/, shaders/, ibl/   (../../resources/ target for Editor.exe)
--     bin/Runtime/Release/x64/        Runtime.exe + all DLLs
--     bin/Runtime/resources/          editor/, shaders/, ibl/   (../../resources/ target for Runtime.exe)
--     bin/Tests/Release/x64/          Tests.exe + DLLs
--     sdk/                            plugin headers + deps + lib/Release/imgui.lib + template
--
-- Editor/Runtime resolve engine assets via "../../resources/" relative to the exe
-- (cwd = the x64/ folder), so resources live at bin/<App>/resources/, two levels
-- up from each exe — NOT at the bundle root. The launcher app-image sits beside
-- bin/ so EditorLauncher's "../bin/Editor/Release/x64/Editor.exe" probe resolves.

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

   -- 4. Resources — placed two levels up from each exe so "../../resources/" resolves.
   --    Tests is CPU-only and needs none.
   n = copyTree("resources", "**", dist .. "/bin/Editor/resources")
   copyTree("resources", "**", dist .. "/bin/Runtime/resources")
   print("  resources (x2): " .. n .. " files each")

   -- 5. SDK — refresh lib/Release/imgui.lib from this Release build, then copy.
   if vfExportPluginSDK then
      vfExportPluginSDK()
   else
      print("  WARNING: vfExportPluginSDK not in scope; copying existing sdk/ as-is")
   end
   n = copyTree("sdk", "**", dist .. "/sdk")
   print("  sdk: " .. n .. " files")

   -- 6. Java launcher → self-contained jpackage app-image (bundled JRE).
   --    Needs Maven + JDK 25 on PATH; the panteleyev plugin goal is jpackage:jpackage.
   print("  Building launcher (mvn package jpackage:jpackage) ...")
   local ok = os.execute('mvn -f launcher/pom.xml clean package jpackage:jpackage')
   local appImage = "launcher/target/dist/VertexForge Launcher"
   if ok and os.isdir(appImage) then
      n = copyTree(appImage, "**", dist .. "/VertexForge Launcher")
      print("  launcher app-image: " .. n .. " files")
   else
      print("  WARNING: launcher app-image not produced (is Maven/JDK 25 on PATH?).")
      print("           Native bundle is complete; re-run after building " .. appImage .. "/.")
   end

   -- 7. Entry-point launcher script (cwd-proof: sets the highest-priority editor lookup).
   io.writefile(dist .. "/VertexForge.bat",
      "@echo off\r\n" ..
      "set \"VERTEXFORGE_EDITOR_PATH=%~dp0bin\\Editor\\Release\\x64\\Editor.exe\"\r\n" ..
      "start \"\" \"%~dp0VertexForge Launcher\\VertexForge Launcher.exe\"\r\n")

   print("Done. Release package at " .. path.getabsolute(dist))
end

newaction {
   trigger = "package-release",
   description = "Assemble the single-folder ship distribution (Editor+Runtime+SDK+resources+Tests+launcher) into dist/VertexForge/",
   execute = vfPackageRelease
}
