-- premake5 package-release [--skip-launcher]
-- Assembles a single, ready-to-ship VertexForge engine/editor distribution into
-- dist/VertexForge/. This is the *engine* package (Editor + Runtime + SDK +
-- resources + Tests + plugins + engine content + Java launcher) — NOT a game export
-- (that's GameExport → .vfpak, which packages an individual game).
--
-- Run order (the user builds Release themselves first):
--   msbuild VFEngine/VertexForge.sln /p:Configuration=Release /p:Platform=x64
--   premake5 package-release                    (builds the launcher: needs JDK 25)
--   premake5 package-release --skip-launcher    (no JDK needed; ships no launcher)
--
-- Bundle layout (the bin/ tree is preserved so existing relative paths resolve):
--   dist/VertexForge/
--     VertexForge Launcher/           jpackage APP_IMAGE (own jlink runtime; sibling of bin/) — entry point
--     bin/Editor/Release/x64/         Editor.exe + all DLLs + engine.json (VK-1680 manifest)
--     bin/Editor/resources/           editor/, shaders/, ibl/   (../../resources/ target for Editor.exe)
--     bin/Runtime/Release/x64/        Runtime.exe + all DLLs
--     bin/Runtime/resources/          editor/, shaders/, ibl/   (../../resources/ target for Runtime.exe)
--     bin/Tests/Release/x64/          Tests.exe + DLLs
--     sdk/                            plugin headers + deps + lib/Release/imgui.lib + template
--     plugins/<Name>/                 <Name>.vfplugin + the Release <Name>.dll (+ assets/, resources/)
--     assets/scripts/lib/             the mType script library projects copy
--     assets/scripts/game/examples/   the example scripts (quoted by the MCP primer; "Include examples")
--     assets/vfx/library/             the VFX library projects copy into assets/vfx/library
--     assets/templates/               the project templates the launcher creates projects from (VK-1689)
--
-- Editor/Runtime resolve engine assets via "../../resources/" relative to the exe
-- (cwd = the x64/ folder), so resources live at bin/<App>/resources/, two levels
-- up from each exe — NOT at the bundle root. The launcher app-image sits beside
-- bin/: the launcher finds its bundled engine from its own exe (jpackage.app-path)
-- as ../bin/Editor/Release/x64/Editor.exe (BundledEngine.java, VK-1681), whatever
-- folder it is started from, and registers it in ~/.vertexforge/engines.json.
--
-- Plugins (VK-1688): PluginManager::resolvePluginsDirectory walks up from the exe and
-- finds dist/VertexForge/plugins four levels up. Each plugins/<Name>/ ships its
-- top-level .vfplugin, the Release DLL from bin/<Name>/Release/x64/ and, like the game
-- exporter (PluginExportPlanner.cpp), its assets/ and resources/ folders. Never shipped:
-- plugins/<Name>/<Name>.dll (the postbuild copy of whatever configuration was built
-- last), plugins/data/ (per-machine plugin state, PluginContextImpl::getPluginDataPath)
-- and the sources. Disabled plugins ship too (the Plugin Manager lists them), with the
-- enabled flags the working tree's descriptors hold. An enabled plugin whose apiVersion is
-- not the engine's fails the package (a disabled one only warns, as PluginManager skips it
-- before that check), and so does a descriptor-less DLL lying in plugins/ itself, which the
-- dev editor's legacy pass would load.
--
-- engine.json (tools/engine_version.lua) records workingDirectory "." (the exe folder)
-- and the content folders vfEngineContentPaths() names, relative to the bundle root,
-- which mirrors the repo layout. Every content folder is copied there and checked byte
-- for byte against the repo, and the staged manifest must resolve to the copies.
--
-- Failures: every check ends in error(), so premake prints "Error: package-release: ..."
-- and exits 1, and dist/VertexForge only ever changes once a new package is complete.
-- The Release build's own engine.json must match the sources (version, asset format, project
-- schema, plugin API, launch contract), or the bundle would claim what its binaries lack.
-- The launcher is built first; the package is assembled in dist/.VertexForge-staging and
-- verified; then the old package is renamed to dist/.VertexForge-old, the staging folder
-- renamed into place, and the old one deleted. When the old package cannot be moved aside
-- (a file in it is open) it stays in place; when the new one then cannot be moved in, the
-- old one is renamed back, and the error says where each package is if that fails too.
--
-- Read-only files: jpackage 25 leaves the launcher exe read-only, os.copyfile (CopyFileW)
-- keeps that flag, and os.rmdir stops at the first file it cannot delete. So every copy
-- is made writable, and trees are made writable before they are deleted.

newoption {
   trigger = "skip-launcher",
   description = "package-release: package without building or shipping the Java launcher (no JDK needed)"
}

function vfPackageRelease()
   local dist = "dist/VertexForge"
   local staging = "dist/.VertexForge-staging"
   local old = "dist/.VertexForge-old"
   local launcherImage = "launcher/target/dist/VertexForge Launcher"
   local launcherExe = launcherImage .. "/VertexForge Launcher.exe"
   local skipLauncher = _OPTIONS["skip-launcher"] ~= nil
   local content = vfEngineContentPaths()
   local warnings = {}
   local distUnchanged = true

   local function fail(msg)
      if distUnchanged then
         msg = msg .. " (" .. dist .. " was not changed)"
      end
      error("package-release: " .. msg, 0)
   end

   local function warn(msg)
      print("  WARNING: " .. msg)
      table.insert(warnings, msg)
   end

   -- An error message without the CRLF Windows system messages end with.
   local function reason(err)
      return (tostring(err or "unknown error"):gsub("%s+$", ""))
   end

   -- Copies one file and makes the copy writable. Returns its size in bytes.
   local function copyFile(src, dst)
      os.mkdir(path.getdirectory(dst))
      local ok, err = os.copyfile(src, dst)
      if not ok then
         fail("cannot copy " .. src .. " to " .. dst .. ": " .. reason(err))
      end
      ok, err = os.chmod(dst, "666")
      if not ok then
         fail("cannot make " .. dst .. " writable: " .. reason(err))
      end
      local stat = os.stat(dst)
      return stat and stat.size or 0
   end

   -- Copies every file under srcBase into dstBase, preserving the tree. Returns { files, bytes }.
   local function copyTree(srcBase, dstBase)
      local files = os.matchfiles(path.join(srcBase, "**"))
      local bytes = 0
      for _, f in ipairs(files) do
         bytes = bytes + copyFile(f, path.join(dstBase, path.getrelative(srcBase, f)))
      end
      return { files = #files, bytes = bytes }
   end

   -- Deletes a tree, clearing read-only flags first. Returns true, or nil and the reason.
   local function removeTree(dir)
      if not os.isdir(dir) then
         return true
      end
      for _, f in ipairs(os.matchfiles(path.join(dir, "**"))) do
         os.chmod(f, "666")
      end
      local _, err = os.rmdir(dir)
      if os.isdir(dir) then
         return nil, reason(err or "files remain")
      end
      return true
   end

   local function requireRemoved(dir)
      local ok, err = removeTree(dir)
      if not ok then
         fail("cannot delete " .. dir .. " (" .. err .. "): close any program using it, then re-run")
      end
   end

   local function verifyFile(src, dst)
      local same, err = os.comparefiles(src, dst)
      if same ~= true then
         fail(dst .. " is not identical to " .. src .. (err and (": " .. err) or ""))
      end
   end

   local function relativeFiles(base)
      local set = {}
      for _, f in ipairs(os.matchfiles(path.join(base, "**"))) do
         set[path.getrelative(base, f)] = true
      end
      return set
   end

   -- The copy must hold exactly the files of the source (none missing, none extra), byte for byte.
   local function verifyTree(srcBase, dstBase)
      local srcFiles, dstFiles = relativeFiles(srcBase), relativeFiles(dstBase)
      for rel in pairs(srcFiles) do
         if not dstFiles[rel] then
            fail(dstBase .. " is missing " .. rel)
         end
         verifyFile(path.join(srcBase, rel), path.join(dstBase, rel))
      end
      for rel in pairs(dstFiles) do
         if not srcFiles[rel] then
            fail(dstBase .. " holds " .. rel .. ", which " .. srcBase .. " does not")
         end
      end
   end

   -- A UTF-8 BOM is skipped like the engine's JSON parser does (some .vfplugin files carry one).
   local function readJson(file)
      local text = io.readfile(file)
      if not text then
         return nil
      end
      local ok, decoded = pcall(json.decode, (text:gsub("^\239\187\191", "")))
      if ok and type(decoded) == "table" then
         return decoded
      end
      return nil
   end

   -- Every plugins/<Dir>/*.vfplugin with its Release DLL, sorted by descriptor path. Mirrors what
   -- PluginManager::loadAll accepts, so the packaged editor lists what the dev editor lists.
   local function collectPlugins()
      -- PluginManager's legacy pass loads a DLL lying directly in plugins/ without a descriptor.
      for _, orphan in ipairs(os.matchfiles("plugins/*.dll")) do
         fail(orphan .. " has no .vfplugin: the dev editor loads it the legacy way, but this package " ..
            "ships only described plugins. Move it to plugins/<Name>/ with a <Name>.vfplugin")
      end
      local apiVersion = tonumber(vfReadPluginApiVersion())
      local descriptors = os.matchfiles("plugins/**.vfplugin")
      table.sort(descriptors)
      local plugins, byName = {}, {}
      for _, descriptor in ipairs(descriptors) do
         local dir = path.getdirectory(descriptor)
         if path.getdirectory(dir) ~= "plugins" then
            fail(descriptor .. " is not at plugins/<Name>/<file>.vfplugin, the only layout this package ships;" ..
               " the dev editor would load it, the packaged one would not")
         end
         local d = readJson(descriptor)
         if not d then
            fail(descriptor .. " is not a JSON object")
         end
         if type(d.name) ~= "string" or type(d.library) ~= "string" then
            fail(descriptor .. " needs string \"name\" and \"library\" fields")
         end
         if d.library:find("[/\\:]") or not d.library:lower():find("%.dll$") then
            fail(descriptor .. ": \"library\" must name a .dll beside the descriptor, got '" .. d.library .. "'")
         end
         -- PluginManager skips a disabled plugin before its API check, and the Plugin Manager
         -- window still lists it, so only an enabled one fails here.
         if tonumber(d.apiVersion) ~= apiVersion then
            local mismatch = descriptor .. " targets plugin API " .. tostring(d.apiVersion) ..
               " but the engine's is " .. apiVersion
            if d.enabled == false then
               warn(mismatch .. "; it is disabled, and enabling it would fail (as in the dev editor)")
            else
               fail(mismatch .. ", so the packaged editor would refuse it")
            end
         end
         if byName[d.name] then
            fail(byName[d.name] .. " and " .. descriptor .. " both describe the plugin '" .. d.name .. "'")
         end
         byName[d.name] = descriptor
         local dll = "bin/" .. d.library:sub(1, -5) .. "/Release/x64/" .. d.library
         local lastBuilt = path.join(dir, d.library)
         if not os.isfile(dll) then
            fail("plugin '" .. d.name .. "' has no Release build: " .. dll .. " is missing. Build Release; " ..
               lastBuilt .. " is a copy of the last-built configuration and never ships")
         end
         -- The postbuild copy keeps the built DLL's time, so a newer copy means another configuration
         -- was built after Release.
         local releaseStat, lastBuiltStat = os.stat(dll), os.isfile(lastBuilt) and os.stat(lastBuilt) or nil
         if releaseStat and lastBuiltStat and releaseStat.mtime < lastBuiltStat.mtime then
            warn(dll .. " is older than " .. lastBuilt .. " (another configuration was built since): " ..
               "rebuild Release if " .. d.name .. " changed")
         end
         local extras = {}
         for _, extra in ipairs({ "assets", "resources" }) do
            if os.isdir(path.join(dir, extra)) then
               table.insert(extras, extra)
            end
         end
         table.insert(plugins, {
            name = d.name,
            dir = dir,
            descriptor = descriptor,
            library = d.library,
            dll = dll,
            enabled = d.enabled ~= false,
            extras = extras
         })
      end
      return plugins
   end

   print("Packaging VertexForge release into " .. dist .. "/ ...")

   -- 1. Preflight — nothing is written before every check has passed.
   for _, exe in ipairs({
      "bin/Editor/Release/x64/Editor.exe",
      "bin/Runtime/Release/x64/Runtime.exe",
      "bin/Tests/Release/x64/Tests.exe",
   }) do
      if not os.isfile(exe) then
         fail("missing " .. exe .. ". Build Release first: " ..
            "msbuild VFEngine/VertexForge.sln /p:Configuration=Release /p:Platform=x64")
      end
   end

   -- The Release Editor's own manifest (its postbuild writes it from the sources it was built with)
   -- must match the sources now, field by field: the bundle's manifest is written from the sources,
   -- and the descriptors are checked against them, so a stale Release build would claim a version,
   -- plugin API or launch contract its binaries do not have.
   local buildManifestPath = "bin/Editor/Release/x64/engine.json"
   local buildManifest = readJson(buildManifestPath)
   if not buildManifest then
      fail(buildManifestPath .. " is missing or unreadable (the Release Editor's postbuild writes it). " ..
         "Rebuild Release")
   end
   local engineVersion = vfReadEngineVersion().text
   for _, field in ipairs({
      { name = "engineVersion", value = engineVersion },
      { name = "assetFormatVersion", value = vfReadAssetFormatVersion() },
      { name = "projectSchemaVersion", value = vfReadProjectSchemaVersion() },
      { name = "pluginApiVersion", value = vfReadPluginApiVersion(), numeric = true },
      { name = "launchContract", value = vfReadEditorLaunchContract(), numeric = true },
      { name = "configuration", value = "Release" },
   }) do
      local built = buildManifest[field.name]
      local same
      if field.numeric then
         same = tonumber(built) ~= nil and tonumber(built) == tonumber(field.value)
      else
         same = built == field.value
      end
      if not same then
         fail("the Release binaries were built with " .. field.name .. " " .. tostring(built) ..
            " but the sources now say " .. field.value .. ". Re-run premake5 vs2022 and rebuild Release")
      end
   end

   local plugins = collectPlugins()

   -- manifestField: the engine.json field that must resolve to the shipped copy.
   local contentTrees = {
      { rel = content.scriptLibDir, required = true, manifestField = "scriptLibDir" },
      { rel = content.examplesDir, required = true, manifestField = "examplesDir" },
      { rel = content.vfxLibraryDir, required = true, manifestField = "vfxLibraryDir" },
      { rel = content.templatesDir, required = true, manifestField = "templatesDir" },
   }
   for _, tree in ipairs(contentTrees) do
      tree.present = os.isdir(tree.rel) and #os.matchfiles(path.join(tree.rel, "**")) > 0
      if tree.required and not tree.present then
         fail(tree.rel .. " is missing or empty")
      end
   end

   if not skipLauncher and not os.isfile("launcher/mvnw.cmd") then
      fail("launcher/mvnw.cmd is missing. Pass --skip-launcher to package without the launcher")
   end

   -- A run whose swap failed half-way can leave the only copy of the previous package here.
   if os.isdir(old) and not os.isdir(dist) then
      fail(old .. " holds the previous package and " .. dist .. " is missing: rename it back to " ..
         path.getname(dist) .. " (or delete it), then re-run")
   end

   -- 2. Java launcher → self-contained jpackage app-image (VK-1692: a jlink runtime of the JDK's
   --    modules, the Gluon JavaFX jmods and Gson, plus the launcher module in app/mods).
   --    Needs JDK 25 (with its jmods, or one that links from its run-time image) on PATH or in
   --    JAVA_HOME; the Maven wrapper (launcher/mvnw.cmd) downloads the pinned Maven, and the image
   --    profile the JavaFX jmods (checksum-verified, cached in ~/.m2/repository/.cache/vertexforge),
   --    on first use. `verify` also runs the launcher's unit tests (VK-1677), so a red test means no
   --    app image. target/dist goes first so that an old image can never pass the checks below.
   if skipLauncher then
      print("  Launcher: skipped (--skip-launcher)")
   else
      requireRemoved("launcher/target/dist")
      print("  Building the launcher (mvnw.cmd -B clean verify -Pimage) ...")
      -- ".\" because cmd skips the current folder when NoDefaultCurrentDirectoryInExePath is set.
      local ok, how, code = os.execute('cd launcher && .\\mvnw.cmd -B clean verify -Pimage')
      if not ok then
         fail("the launcher build failed (mvnw.cmd " .. tostring(how) .. " " .. tostring(code) .. "). " ..
            "It needs JDK 25 on PATH or in JAVA_HOME, runs the launcher's unit tests, and on first use " ..
            "downloads the JavaFX jmods. Pass --skip-launcher to package without the launcher")
      end
      if not os.isfile(launcherExe) then
         fail("the launcher build succeeded but produced no " .. launcherExe)
      end
      -- The launcher reads VERSION itself (its pom's image profile); the two readers must agree.
      local launcherCfg = launcherImage .. "/app/VertexForge Launcher.cfg"
      local cfgText = io.readfile(launcherCfg)
      local launcherVersion = cfgText and cfgText:match("java%-options=%-Djpackage%.app%-version=([^\r\n]*)")
      if launcherVersion ~= engineVersion then
         fail(launcherCfg .. " says jpackage.app-version " .. tostring(launcherVersion) ..
            " but VERSION says " .. engineVersion)
      end
   end

   -- 3. SDK — refresh lib/Release/imgui.lib from this Release build (copied in step 4).
   if vfExportPluginSDK then
      vfExportPluginSDK()
   else
      warn("vfExportPluginSDK not in scope; copying the existing sdk/ as it is")
   end

   -- 4. Stage the whole package. A leftover .VertexForge-old here is a previous package whose
   --    deletion failed after a successful swap (dist/VertexForge exists, checked above).
   print("  Staging into " .. staging .. "/ ...")
   requireRemoved(staging)
   requireRemoved(old)
   os.mkdir(staging)

   local sections = {}
   local function section(label, stats, note)
      table.insert(sections, { label = label, stats = stats, note = note })
   end

   -- Binaries (exe + every DLL beside it: subsystems, OpenAL, streamline, assimp).
   for _, app in ipairs({ "Editor", "Runtime", "Tests" }) do
      local rel = "bin/" .. app .. "/Release/x64"
      section(rel, copyTree(rel, path.join(staging, rel)))
   end

   -- engine.json (VK-1680). The binaries brought the dev-tree manifest; rewrite it for the
   -- bundle: working directory = the exe folder, content paths under the bundle root. Its paths
   -- are relative to the exe folder, so they stay valid once staging is renamed to dist/VertexForge.
   -- gitCommit = the commit the Release Editor was built at, or HEAD when its manifest has none.
   local stagedExeDir = path.getabsolute(path.join(staging, "bin/Editor/Release/x64"))
   local gitCommit = type(buildManifest.gitCommit) == "string" and buildManifest.gitCommit or vfGitCommit()
   vfWriteEngineManifest {
      exeDir = stagedExeDir,
      workingDir = stagedExeDir,
      contentRoot = path.getabsolute(staging),
      configuration = "Release",
      gitCommit = gitCommit
   }

   -- Resources — placed two levels up from each exe so "../../resources/" resolves.
   -- Tests is CPU-only and needs none.
   for _, app in ipairs({ "Editor", "Runtime" }) do
      local rel = "bin/" .. app .. "/resources"
      section(rel, copyTree("resources", path.join(staging, rel)))
   end

   -- Splash logo — SplashScreen::loadSplashImage() searches exe-relative paths
   -- (4-up to the dev-tree resources/, then beside the exe), NOT the engine's
   -- cwd-relative "../../resources/". Drop the PNG beside Editor.exe so the
   -- beside-the-exe fallback resolves in the packaged layout.
   section("splash logo (beside Editor.exe)", {
      files = 1,
      bytes = copyFile("resources/editor/VertexForge-logo.png",
         path.join(staging, "bin/Editor/Release/x64/VertexForge-logo.png"))
   })

   section("sdk", copyTree("sdk", path.join(staging, "sdk")))

   -- Plugins: descriptor + Release DLL (+ assets/, resources/), at the same plugins/<Dir>/ path.
   local pluginStats = { files = 0, bytes = 0 }
   local pluginNames = {}
   for _, p in ipairs(plugins) do
      local dstDir = path.join(staging, p.dir)
      pluginStats.bytes = pluginStats.bytes + copyFile(p.descriptor, path.join(staging, p.descriptor))
      pluginStats.bytes = pluginStats.bytes + copyFile(p.dll, path.join(dstDir, p.library))
      pluginStats.files = pluginStats.files + 2
      for _, extra in ipairs(p.extras) do
         local stats = copyTree(path.join(p.dir, extra), path.join(dstDir, extra))
         pluginStats.files = pluginStats.files + stats.files
         pluginStats.bytes = pluginStats.bytes + stats.bytes
      end
      table.insert(pluginNames, p.name .. (p.enabled and "" or " (disabled)"))
   end
   section("plugins", pluginStats, #plugins .. " plugins: " .. table.concat(pluginNames, ", "))

   -- Engine content, at the same path under the bundle root as in the repo.
   for _, tree in ipairs(contentTrees) do
      if tree.present then
         section(tree.rel, copyTree(tree.rel, path.join(staging, tree.rel)), "byte-identical to the repo")
      else
         section(tree.rel, nil, (os.isdir(tree.rel) and "empty" or "not in the repo yet") .. ", skipped")
      end
   end

   if skipLauncher then
      section("VertexForge Launcher", nil, "SKIPPED (--skip-launcher)")
   else
      section("VertexForge Launcher", copyTree(launcherImage, path.join(staging, "VertexForge Launcher")))
   end

   -- 5. Verify the copies against their sources, and the staged manifest against the copies.
   print("  Verifying ...")
   for _, tree in ipairs(contentTrees) do
      if tree.present then
         verifyTree(tree.rel, path.join(staging, tree.rel))
      end
   end
   -- A template's project/.gitignore is checked by name too: verifyTree compares the file lists that
   -- os.matchfiles returns, so a dot file skipped on both sides would pass it unnoticed (VK-1689).
   for _, templateDir in ipairs(os.matchdirs(path.join(content.templatesDir, "*"))) do
      local gitignore = path.join(templateDir, "project/.gitignore")
      if os.isfile(gitignore) then
         local staged = path.join(staging, gitignore)
         if not os.isfile(staged) then
            fail(staged .. " is missing: the package must ship every template's project/.gitignore")
         end
         verifyFile(gitignore, staged)
      end
   end
   for _, p in ipairs(plugins) do
      verifyFile(p.descriptor, path.join(staging, p.descriptor))
      verifyFile(p.dll, path.join(staging, p.dir, p.library))
      for _, extra in ipairs(p.extras) do
         verifyTree(path.join(p.dir, extra), path.join(staging, p.dir, extra))
      end
   end

   local stagedManifestPath = path.join(stagedExeDir, "engine.json")
   local stagedManifest = readJson(stagedManifestPath)
   if not stagedManifest then
      fail(stagedManifestPath .. " is missing or unreadable")
   end
   local function checkManifestDir(field, rel)
      local value = stagedManifest[field]
      local target = type(value) == "string" and path.getabsolute(path.join(stagedExeDir, value)) or nil
      if target ~= path.getabsolute(path.join(staging, rel)) or not os.isdir(target) then
         fail(stagedManifestPath .. " " .. field .. " = " .. tostring(value) .. " does not resolve to the shipped " .. rel)
      end
   end
   for _, tree in ipairs(contentTrees) do
      if tree.present and tree.manifestField then
         checkManifestDir(tree.manifestField, tree.rel)
      end
   end

   -- 6. Swap: the old package moves aside in one rename, so an open file inside it fails the
   --    rename and leaves it whole.
   local hadPrevious = os.isdir(dist)
   if hadPrevious then
      local ok, err = os.rename(dist, old)
      if not ok then
         fail("cannot replace " .. dist .. " (" .. reason(err) .. "): close any Editor, Runtime, launcher " ..
            "or Explorer window using it, then re-run. The new package is complete in " .. staging)
      end
      distUnchanged = false
   end
   local moved, moveErr = os.rename(staging, dist)
   if not moved then
      local outcome
      if not hadPrevious then
         outcome = "The new package is complete in " .. staging .. "."
      elseif os.rename(old, dist) == true then
         outcome = "The previous package is back in place."
      else
         outcome = "The previous package is in " .. old .. "."
      end
      fail("cannot move " .. staging .. " to " .. dist .. " (" .. reason(moveErr) .. "). " .. outcome)
   end
   local removed, removeErr = removeTree(old)
   if not removed then
      warn("the previous package is left in " .. old .. " (" .. removeErr .. "); the next run deletes it")
   end

   -- 7. Summary.
   local function describe(stats)
      return string.format("%d %s, %.1f MiB", stats.files, stats.files == 1 and "file" or "files",
         stats.bytes / (1024 * 1024))
   end
   print("")
   print("Packaged VertexForge " .. engineVersion .. " (Release, commit " ..
      (gitCommit and gitCommit:sub(1, 9) or "unknown") .. ") into " .. path.getabsolute(dist) .. ":")
   for _, s in ipairs(sections) do
      local text = s.stats and describe(s.stats) or ""
      if s.note then
         text = text ~= "" and (text .. "  " .. s.note) or s.note
      end
      print(string.format("  %-32s %s", s.label, text))
   end
   if #warnings > 0 then
      print("  " .. #warnings .. " warning(s):")
      for _, w in ipairs(warnings) do
         print("    " .. w)
      end
   end
   if skipLauncher then
      print("Run: \"" .. dist .. "/bin/Editor/Release/x64/Editor.exe\" (no launcher: --skip-launcher)")
   else
      print("Run: \"" .. dist .. "/VertexForge Launcher/VertexForge Launcher.exe\"")
   end
end

newaction {
   trigger = "package-release",
   description = "Assemble the single-folder ship distribution (Editor+Runtime+SDK+resources+Tests+plugins+engine content+launcher) into dist/VertexForge/",
   execute = vfPackageRelease
}
