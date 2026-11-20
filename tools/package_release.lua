-- premake5 package-release [--skip-launcher] [--skip-msi]
-- Assembles a single, ready-to-ship VertexForge engine/editor distribution into
-- dist/VertexForge/, and its per-user installer dist/VertexForge-<VERSION>.msi. This is the *engine* package (Editor +
-- Runtime + SDK + resources + Tests + plugins + engine content + Java launcher) — NOT a game export
-- (that's GameExport → .vfpak, which packages an individual game).
--
-- Run order (the user builds Release themselves first):
--   msbuild VFEngine/VertexForge.sln /p:Configuration=Release /p:Platform=x64
--   premake5 package-release                    (launcher + installer: needs JDK 25.0.2+ and the WiX Toolset)
--   premake5 package-release --skip-msi         (launcher, no installer: needs JDK 25, no WiX)
--   premake5 package-release --skip-launcher    (no JDK needed; ships no launcher and so no installer)
--
-- Bundle layout (the bin/ tree is preserved so existing relative paths resolve). The bundle root IS the launcher's
-- jpackage app image (VK-1693): jpackage --type msi installs an app image as the install folder and needs its
-- launcher at the root, so the installer installs exactly this folder.
--   dist/VertexForge/
--     VertexForge Launcher.exe        the Java launcher (jpackage app image, own jlink runtime) — entry point
--     app/, runtime/                  the launcher's module + .cfg, and its runtime
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
--   dist/VertexForge-<VERSION>.msi    the installer (VK-1693; not with --skip-msi or --skip-launcher)
--
-- Editor/Runtime resolve engine assets via "../../resources/" relative to the exe
-- (cwd = the x64/ folder), so resources live at bin/<App>/resources/, two levels
-- up from each exe — NOT at the bundle root. The launcher finds its bundled engine from its own exe
-- (jpackage.app-path) as bin/Editor/Release/x64/Editor.exe below the exe's folder (BundledEngine.java, VK-1681,
-- VK-1693), whatever folder it is started from, and registers it in ~/.vertexforge/engines.json.
--
-- Exe folders: the Editor, Runtime and Tests write logs/, crashes/, cache/, saves/ and imgui.ini into their exe
-- folder at run time, and the linker leaves .pdb/.ilk files there. None of them ships (shippedBinFile): a package
-- must not carry one machine's state or a gigabyte of symbols. Each copy is verified against its filtered source.
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
-- Installer (VK-1693): jpackage --type msi --app-image <the verified staging folder>, before the swap.
--   * It installs per user (no UAC) into %LOCALAPPDATA%\Programs\VertexForge, adds the Start menu entry
--     VertexForge > VertexForge Launcher, and opens .vfproj files in the launcher ("%1": the launcher adds the
--     project and opens it, or hands it to the launcher already running, VK-1685). The upgrade code and the install
--     folder are PERMANENT (vfMsiUpgradeCode, vfMsiInstallDir): a newer MSI replaces an older one in place only
--     while both stay the same. VERSION must fit an MSI product version (at most 255.255.65535).
--   * It runs the jpackage of the JDK Maven builds the launcher with (JAVA_HOME, else java.exe on PATH, asked for
--     its java.home): jpackage packages only app images its own version built (app/.jpackage.xml). JDK 25.0.0 and
--     25.0.1 are refused: they drop --file-associations when packaging an app image (JDK-8372753).
--   * The WiX Toolset must be where jpackage looks (PATH, %USERPROFILE%\.dotnet\tools, "WiX Toolset v*\bin" under
--     the Program Files folders); WiX 3.14.1 is the one the docs recommend.
--   * tools/msi_inspect.ps1 then reads the MSI's own tables and every fact is checked (version, upgrade code,
--     per-user, install folder, every staged file, the Start menu shortcut, the .vfproj association).
--   * jpackage's MSI deletes the WHOLE install folder on uninstall and on every upgrade (util:RemoveFolderEx; the
--     old version is removed before the new one installs), including what the editor writes there. The summary says
--     so; the project folders and ~/.vertexforge are never touched.
--   * After the swap the MSI is moved to dist/VertexForge-<VERSION>.msi, replacing one of the same version; the
--     installers of other versions are left alone.
--
-- Failures: every check ends in error(), so premake prints "Error: package-release: ..."
-- and exits 1, and dist/VertexForge only ever changes once a new package is complete.
-- The Release build's own engine.json must match the sources (version, asset format, project
-- schema, plugin API, launch contract), or the bundle would claim what its binaries lack.
-- The launcher is built first; the package is assembled in dist/.VertexForge-staging and
-- verified, and the installer is built from it in dist/.VertexForge-installer and inspected;
-- then the old package is renamed to dist/.VertexForge-old, the staging folder
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

newoption {
   trigger = "skip-msi",
   description = "package-release: do not build the MSI installer (no WiX needed)"
}

-- The installer's identity (VK-1693). PERMANENT: every VertexForge MSI ever shipped must carry this upgrade code and
-- install folder, or a newer MSI no longer replaces an older one in place. Never change either.
local vfMsiUpgradeCode = "4e0f9be5-20f3-4617-bafd-1e0a49993c67"
local vfMsiInstallDir = "Programs\\VertexForge"   -- relative to %LOCALAPPDATA%: a per-user install
-- The launcher image's main launcher (jpackage --name in launcher/pom.xml): JDK-8372994 keeps the .vfproj association
-- only when the MSI's --name is that name. It is also the product name Apps & features shows.
local vfMsiName = "VertexForge Launcher"
local vfMsiVendor = "VertexForge"
local vfMsiMenuGroup = "VertexForge"
local vfMsiDescription = "VertexForge game engine: editor, runtime and launcher"
local vfMsiInspectScript = path.join(_SCRIPT_DIR, "msi_inspect.ps1")

function vfPackageRelease()
   local dist = "dist/VertexForge"
   local staging = "dist/.VertexForge-staging"
   local old = "dist/.VertexForge-old"
   local installerWork = "dist/.VertexForge-installer"
   local launcherImage = "launcher/target/dist/VertexForge Launcher"
   local launcherExeName = "VertexForge Launcher.exe"
   local launcherExe = launcherImage .. "/" .. launcherExeName
   local launcherIcon = "launcher/src/jpackage/VertexForge Launcher.ico"
   local skipLauncher = _OPTIONS["skip-launcher"] ~= nil
   local skipMsi = _OPTIONS["skip-msi"] ~= nil
   local buildMsi = not skipLauncher and not skipMsi
   local content = vfEngineContentPaths()
   local warnings = {}
   local distUnchanged = true

   local function fail(msg)
      if distUnchanged then
         msg = msg .. " (" .. dist .. " was not changed)"
      end
      error("package-release: " .. msg, 0)
   end

   local function failInstaller(msg)
      fail(msg .. ". Pass --skip-msi to package without the installer")
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

   -- Copies every file under srcBase into dstBase, preserving the tree; keep(rel), when given, picks the files by
   -- their "/"-separated path below srcBase. Returns { files, bytes, skipped = { files, bytes, names } }, where
   -- names counts the skipped files by their top-level folder ("logs/") or their name.
   local function copyTree(srcBase, dstBase, keep)
      local stats = { files = 0, bytes = 0, skipped = { files = 0, bytes = 0, names = {} } }
      for _, f in ipairs(os.matchfiles(path.join(srcBase, "**"))) do
         local rel = path.getrelative(srcBase, f)
         if keep == nil or keep(rel) then
            stats.bytes = stats.bytes + copyFile(f, path.join(dstBase, rel))
            stats.files = stats.files + 1
         else
            local stat = os.stat(f)
            local top = rel:match("^([^/]+)/")
            local name = top and (top .. "/") or rel
            stats.skipped.files = stats.skipped.files + 1
            stats.skipped.bytes = stats.skipped.bytes + (stat and stat.size or 0)
            stats.skipped.names[name] = (stats.skipped.names[name] or 0) + 1
         end
      end
      return stats
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

   -- The "/"-separated paths of the files below base (those keep(rel) accepts, when given), as a set.
   local function relativeFiles(base, keep)
      local set = {}
      for _, f in ipairs(os.matchfiles(path.join(base, "**"))) do
         local rel = path.getrelative(base, f)
         if keep == nil or keep(rel) then
            set[rel] = true
         end
      end
      return set
   end

   -- The copy must hold exactly the files of the source that keep accepts (none missing, none extra), byte for byte.
   local function verifyTree(srcBase, dstBase, keep)
      local srcFiles, dstFiles = relativeFiles(srcBase, keep), relativeFiles(dstBase)
      for rel in pairs(srcFiles) do
         if not dstFiles[rel] then
            fail(dstBase .. " is missing " .. rel)
         end
         verifyFile(path.join(srcBase, rel), path.join(dstBase, rel))
      end
      for rel in pairs(dstFiles) do
         if not srcFiles[rel] then
            fail(dstBase .. " holds " .. rel .. ", which " .. srcBase .. " does not" .. (keep and " ship" or ""))
         end
      end
   end

   -- What the Editor, Runtime and Tests write into their exe folder at run time — logs/ (Log.hpp), crashes/
   -- (CrashHandler.cpp), cache/ (the Runtime's pipeline cache, Device.cpp), saves/ (SaveService.cpp's fallback) and
   -- imgui.ini (ImguiRender.cpp) — and the linker's .pdb/.ilk files never ship (VK-1693). rel is below the exe
   -- folder, "/"-separated.
   local function shippedBinFile(rel)
      local lower = rel:lower()
      local top = lower:match("^([^/]+)/")
      if top == "logs" or top == "crashes" or top == "cache" or top == "saves" then
         return false
      end
      if lower == "imgui.ini" then
         return false
      end
      local ext = path.getextension(lower)
      return ext ~= ".pdb" and ext ~= ".ilk"
   end

   local function describeBytes(bytes)
      return string.format("%.1f MiB", bytes / (1024 * 1024))
   end

   -- "skipped 5 files (440.1 MiB): Editor.pdb, imgui.ini, logs/ (3)", or nil when nothing was skipped.
   local function skippedNote(skipped)
      if skipped.files == 0 then
         return nil
      end
      local names = {}
      for name, count in pairs(skipped.names) do
         table.insert(names, count > 1 and string.format("%s (%d)", name, count) or name)
      end
      table.sort(names)
      return string.format("skipped %d %s (%s): %s", skipped.files, skipped.files == 1 and "file" or "files",
         describeBytes(skipped.bytes), table.concat(names, ", "))
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

   -- One argument for cmd /c, in quotes. cmd expands %VAR% even inside quotes and a quote cannot be escaped, so a
   -- value holding either is refused rather than passed on damaged.
   local function cmdQuote(s)
      if s:find('[%%"]') then
         fail("cannot pass " .. s .. " on a command line: it holds a % or a \"")
      end
      return '"' .. s .. '"'
   end

   -- An absolute path with backslashes, as Windows tools want it.
   local function winPath(p)
      return path.translate(path.getabsolute(p), "\\")
   end

   -- Runs a command line through cmd /c and returns everything it printed (stdout and stderr) and whether it exited 0.
   -- cmd /c strips the first and the last quote of a line that starts with one, so the line gets one more pair.
   local function runCaptured(command)
      local pipe = io.popen('"' .. command .. ' 2>&1"')
      if not pipe then
         return "", false
      end
      local output = pipe:read("*a") or ""
      return output, pipe:close() == true
   end

   -- "25.0.2" -> 25, 0, 2 (a missing part is 0); nil when the text does not start with a number.
   local function parseJavaVersion(text)
      local feature = tonumber(text:match("^(%d+)"))
      if not feature then
         return nil
      end
      return feature, tonumber(text:match("^%d+%.(%d+)")) or 0, tonumber(text:match("^%d+%.%d+%.(%d+)")) or 0
   end

   -- Whether the dotted version a is at least b ("3.14.1.8722" >= "3.0").
   local function versionAtLeast(a, b)
      local x, y = {}, {}
      for n in a:gmatch("%d+") do
         table.insert(x, tonumber(n))
      end
      for n in b:gmatch("%d+") do
         table.insert(y, tonumber(n))
      end
      for i = 1, math.max(#x, #y) do
         local p, q = x[i] or 0, y[i] or 0
         if p ~= q then
            return p > q
         end
      end
      return true
   end

   -- A value for a Java .properties file: backslashes doubled, every non-ASCII character as \uXXXX (UTF-16).
   local function propertiesValue(text)
      local out = {}
      for _, c in utf8.codes(text) do
         if c == 92 then
            table.insert(out, "\\\\")
         elseif c < 128 then
            table.insert(out, string.char(c))
         elseif c < 0x10000 then
            table.insert(out, string.format("\\u%04x", c))
         else
            local v = c - 0x10000
            table.insert(out, string.format("\\u%04x\\u%04x", 0xD800 + (v >> 10), 0xDC00 + (v & 0x3FF)))
         end
      end
      return table.concat(out)
   end

   -- The JDK the launcher build uses: Maven's mvn.cmd runs %JAVA_HOME%\bin\java.exe when JAVA_HOME is set, else the
   -- java.exe PATH finds (for Oracle's installer that is a javapath stub, so the JVM is asked for its java.home).
   -- Returns { home, version, jpackage }.
   local function findJdk()
      local java
      local javaHome = (os.getenv("JAVA_HOME") or ""):gsub('"', "")
      javaHome = javaHome:gsub("^%s+", ""):gsub("%s+$", "")
      if javaHome ~= "" then
         java = path.join(javaHome, "bin/java.exe")
         if not os.isfile(java) then
            failInstaller("JAVA_HOME is " .. javaHome .. " but " .. java .. " does not exist (Maven would fail too)")
         end
      else
         local dir = os.pathsearch("java.exe", os.getenv("PATH") or "")
         if not dir then
            failInstaller("JAVA_HOME is not set and no java.exe is on PATH: the installer needs a JDK 25")
         end
         java = path.join(dir, "java.exe")
      end
      local output, ok = runCaptured(cmdQuote(winPath(java)) .. " -XshowSettings:properties -version")
      local home = output:match("[\r\n]%s*java%.home = ([^\r\n]+)")
      local version = output:match("[\r\n]%s*java%.version = ([^\r\n]+)")
      -- "Java(TM) SE Runtime Environment" for Oracle JDK, "OpenJDK Runtime Environment" for OpenJDK builds.
      local runtimeName = output:match("[\r\n]%s*java%.runtime%.name = ([^\r\n]+)") or ""
      if not ok or not home or not version then
         failInstaller("cannot read java.home and java.version from " .. java .. " -XshowSettings:properties -version: " ..
            reason(output))
      end
      home = home:gsub("%s+$", "")
      version = version:gsub("%s+$", "")
      local jpackage = path.join(home, "bin/jpackage.exe")
      if not os.isfile(jpackage) then
         failInstaller(java .. " runs the JDK in " .. home .. ", which has no bin\\jpackage.exe; the installer needs a " ..
            "full JDK 25")
      end
      return { home = home, version = version, jpackage = jpackage, oracle = runtimeName:find("Java(TM)", 1, true) ~= nil }
   end

   -- The WiX Toolset jpackage 25 would use (WixTool.createToolset): a toolset on PATH alone first, WiX v4+ (wix.exe
   -- 4.0.4 or newer) before WiX v3 (candle.exe + light.exe 3.0 or newer); only when PATH has none, the newest one in
   -- PATH, %USERPROFILE%\.dotnet\tools (wix.exe as a .NET tool) and "WiX Toolset v*\bin" under %ProgramFiles% and
   -- %ProgramFiles(x86)%, again v4+ before v3. Returns { kind = "v4+" | "v3", version, dir }, or fails.
   local function findWix()
      local pathDirs, allDirs, seen = {}, {}, {}
      local function addDir(list, dir)
         if dir and dir ~= "" then
            dir = path.translate((dir:gsub('"', "")), "/")
            if not seen[dir:lower()] then
               seen[dir:lower()] = true
               table.insert(list, dir)
            end
         end
      end
      for _, dir in ipairs(string.explode(os.getenv("PATH") or "", ";")) do
         addDir(pathDirs, dir)
      end
      for _, dir in ipairs(pathDirs) do
         table.insert(allDirs, dir)
      end
      if os.getenv("USERPROFILE") then
         addDir(allDirs, path.join(path.translate(os.getenv("USERPROFILE"), "/"), ".dotnet/tools"))
      end
      for _, variable in ipairs({ "ProgramFiles", "ProgramFiles(x86)" }) do
         local root = os.getenv(variable)
         if root then
            for _, toolset in ipairs(os.matchdirs(path.join(path.translate(root, "/"), "WiX Toolset v*"))) do
               addDir(allDirs, path.join(toolset, "bin"))
            end
         end
      end

      local function versionOf(exe, args, pattern)
         local output = runCaptured(cmdQuote(winPath(exe)) .. " " .. args)
         return output:match(pattern)
      end

      -- Every toolset in dirs that jpackage would accept; one whose version cannot be read counts ("unknown"), as
      -- jpackage checks it again itself. Too old ones are remembered for the error message.
      local tooOld, tooOldSeen = {}, {}
      local function toolsets(dirs)
         local list = {}
         for _, dir in ipairs(dirs) do
            for _, kind in ipairs({ "v4+", "v3" }) do
               local tool, args, pattern, minimum
               if kind == "v4+" and os.isfile(path.join(dir, "wix.exe")) then
                  tool, args, pattern, minimum = path.join(dir, "wix.exe"), "--version", "^%s*(%d+%.%d+[%.%d]*)", "4.0.4"
               elseif kind == "v3" and os.isfile(path.join(dir, "candle.exe")) and os.isfile(path.join(dir, "light.exe")) then
                  tool, args, pattern, minimum = path.join(dir, "candle.exe"), "-?", "version (%d+%.%d+[%.%d]*)", "3.0"
               end
               if tool then
                  local version = versionOf(tool, args, pattern)
                  if version and not versionAtLeast(version, minimum) then
                     if not tooOldSeen[tool:lower()] then
                        tooOldSeen[tool:lower()] = true
                        table.insert(tooOld, tool .. " " .. version)
                     end
                  else
                     table.insert(list, { kind = kind, version = version or "unknown", dir = dir })
                  end
               end
            end
         end
         return list
      end
      -- v4+ before v3; within a kind the first in the list (PATH order), or the newest one when newest is set.
      local function pick(list, newest)
         for _, kind in ipairs({ "v4+", "v3" }) do
            local best
            for _, candidate in ipairs(list) do
               if candidate.kind == kind then
                  if not best then
                     best = candidate
                  elseif newest and candidate.version ~= "unknown" and
                        (best.version == "unknown" or not versionAtLeast(best.version, candidate.version)) then
                     best = candidate
                  end
               end
            end
            if best then
               return best
            end
         end
         return nil
      end
      local found = pick(toolsets(pathDirs), false) or pick(toolsets(allDirs), true)
      if not found then
         failInstaller("the installer needs the WiX Toolset, and " ..
            (#tooOld > 0 and ("only versions jpackage refuses were found (" .. table.concat(tooOld, ", ") .. ")")
               or "none was found") .. " where jpackage looks (candle.exe + light.exe or wix.exe on PATH, " ..
            "%USERPROFILE%\\.dotnet\\tools, \"%ProgramFiles%\\WiX Toolset v*\\bin\", " ..
            "\"%ProgramFiles(x86)%\\WiX Toolset v*\\bin\"). Install WiX 3.14.1 " ..
            "(https://github.com/wixtoolset/wix3/releases/tag/wix3141rtm: wix314.exe installs into " ..
            "C:\\Program Files (x86)\\WiX Toolset v3.14\\bin, where jpackage finds it), then open a new terminal")
      end
      if found.kind == "v4+" then
         local extensions = runCaptured(cmdQuote(winPath(path.join(found.dir, "wix.exe"))) .. " extension list -g")
         if not extensions:find("WixToolset.Util.wixext", 1, true) then
            warn("wix.exe " .. found.version .. " does not list WixToolset.Util.wixext, which jpackage needs: " ..
               "wix extension add -g WixToolset.Util.wixext/" ..
               (found.version:match("^(%d+%.%d+%.%d+)") or "<its version>") ..
               " (or install WiX 3.14.1, the version these docs use)")
         end
         if found.version ~= "unknown" and not versionAtLeast("6.99", found.version) then
            warn("WiX " .. found.version .. " has not been tried with jpackage 25 (WiX 7 needs `wix eula accept " ..
               "wix7` first); WiX 3.14.1 is the version these docs use")
         end
      end
      return found
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
   local engineVersionParts = vfReadEngineVersion()
   local engineVersion = engineVersionParts.text
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

   -- The installer (VK-1693): its version, tools and JDK, all before anything is built or written.
   local msiTarget = "dist/VertexForge-" .. engineVersion .. ".msi"
   local powershell = path.join(os.getenv("SystemRoot") or "C:/Windows", "System32/WindowsPowerShell/v1.0/powershell.exe")
   local jdk, wix
   if buildMsi then
      if tonumber(engineVersionParts.major) > 255 or tonumber(engineVersionParts.minor) > 255 or
            tonumber(engineVersionParts.patch) > 65535 then
         failInstaller("VERSION " .. engineVersion .. " is not a Windows Installer product version " ..
            "(MAJOR and MINOR at most 255, PATCH at most 65535)")
      end
      for _, required in ipairs({ vfMsiInspectScript, powershell, launcherIcon }) do
         if not os.isfile(required) then
            failInstaller("the installer needs " .. required .. ", which is missing")
         end
      end
      if path.getabsolute("."):find('[%%"]') then
         failInstaller("the repository path " .. path.getabsolute(".") .. " holds a % or a \", which cannot be " ..
            "passed to jpackage through cmd")
      end
      jdk = findJdk()
      local feature, interim, update = parseJavaVersion(jdk.version)
      if not feature or feature < 25 then
         failInstaller("the JDK in " .. jdk.home .. " is " .. jdk.version .. "; the launcher and its installer need JDK 25")
      end
      if feature == 25 and interim == 0 and update <= 1 then
         failInstaller("jpackage " .. jdk.version .. " (" .. jdk.home .. ") drops --file-associations when it builds " ..
            "an MSI from an app image (JDK-8372753, fixed in OpenJDK 25.0.2 and Oracle JDK 25.0.3), so the installer " ..
            "would not open .vfproj files. Install a newer JDK 25 (for example Temurin 25.0.5) and point JAVA_HOME at " ..
            "it: Maven builds the launcher with the same JDK")
      end
      if jdk.oracle and feature == 25 and interim == 0 and update == 2 then
         warn("Oracle JDK 25.0.2 may still leave the .vfproj association out of the installer (JDK-8372753 is " ..
            "listed as fixed in Oracle JDK 25.0.3); the installer check below will tell")
      end
      wix = findWix()
      print("  Installer: jpackage " .. jdk.version .. " (" .. jdk.home .. "), WiX " .. wix.version .. " (" ..
         wix.dir .. ")")
   else
      print("  Installer: skipped (" .. (skipLauncher and "--skip-launcher: the installer installs the launcher" or
         "--skip-msi") .. ")")
      if os.isfile(msiTarget) then
         warn(msiTarget .. " is from an earlier run: it does not install the package this run makes")
      end
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
      -- The image becomes the bundle root (VK-1693), so it may hold nothing that could meet bin/, sdk/, plugins/
      -- or assets/ there.
      local top = {}
      for _, f in ipairs(os.matchfiles(launcherImage .. "/*")) do
         table.insert(top, path.getname(f))
      end
      for _, d in ipairs(os.matchdirs(launcherImage .. "/*")) do
         table.insert(top, path.getname(d) .. "/")
      end
      table.sort(top)
      if table.concat(top, ", ") ~= launcherExeName .. ", app/, runtime/" then
         fail(launcherImage .. " holds " .. table.concat(top, ", ") .. ", but the bundle root takes exactly " ..
            launcherExeName .. ", app/ and runtime/ from it")
      end
      -- jpackage packages only an app image its own version built, and keeps the .vfproj association only when the
      -- MSI's --name is the image's main launcher (JDK-8372994).
      if buildMsi then
         local stateFile = launcherImage .. "/app/.jpackage.xml"
         local state = io.readfile(stateFile) or ""
         local stateVersion = state:match('<jpackage%-state[^>]-%sversion="([^"]*)"')
         local platform = state:match('<jpackage%-state[^>]-%splatform="([^"]*)"')
         local mainLauncher = state:match("<main%-launcher>%s*(.-)%s*</main%-launcher>")
         if stateVersion ~= jdk.version then
            failInstaller(stateFile .. " says the image was built by jpackage " .. tostring(stateVersion) ..
               ", but the installer would be built by jpackage " .. jdk.version .. " (" .. jdk.home .. "), and " ..
               "jpackage packages only images of its own version: let Maven and package-release use the same JDK " ..
               "(JAVA_HOME)")
         end
         if platform ~= "windows" or mainLauncher ~= vfMsiName then
            failInstaller(stateFile .. " names platform " .. tostring(platform) .. " and main launcher " ..
               tostring(mainLauncher) .. ", but the installer needs a windows image whose main launcher is " .. vfMsiName)
         end
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
   requireRemoved(installerWork)
   os.mkdir(staging)

   local sections = {}
   local function section(label, stats, note)
      table.insert(sections, { label = label, stats = stats, note = note })
   end

   -- Binaries (exe + every DLL beside it: subsystems, OpenAL, streamline, assimp), without what the exes wrote there
   -- and without debug files. Each copy is checked now: engine.json is rewritten and the splash logo added below.
   for _, app in ipairs({ "Editor", "Runtime", "Tests" }) do
      local rel = "bin/" .. app .. "/Release/x64"
      local dst = path.join(staging, rel)
      local stats = copyTree(rel, dst, shippedBinFile)
      verifyTree(rel, dst, shippedBinFile)
      section(rel, stats, skippedNote(stats.skipped))
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

   -- The launcher's app image is the bundle root (VK-1693): its exe, app/ (with the .jpackage.xml jpackage --type msi
   -- requires) and runtime/ go to the top of the package.
   if skipLauncher then
      section("launcher (bundle root)", nil, "SKIPPED (--skip-launcher)")
   else
      local stats = { files = 1, bytes = copyFile(launcherExe, path.join(staging, launcherExeName)) }
      for _, sub in ipairs({ "app", "runtime" }) do
         local subStats = copyTree(path.join(launcherImage, sub), path.join(staging, sub))
         stats.files = stats.files + subStats.files
         stats.bytes = stats.bytes + subStats.bytes
      end
      section("launcher (bundle root)", stats, launcherExeName .. ", app/, runtime/")
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
   if not skipLauncher then
      verifyFile(launcherExe, path.join(staging, launcherExeName))
      for _, sub in ipairs({ "app", "runtime" }) do
         verifyTree(path.join(launcherImage, sub), path.join(staging, sub))
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

   -- 6. The installer (VK-1693), from the verified staging folder, before anything in dist/ changes.
   local builtMsi
   if buildMsi then
      print("  Building the installer (jpackage --type msi with WiX " .. wix.version .. "; this takes a few minutes) ...")
      os.mkdir(installerWork)
      -- Associations need a MIME type in JDK 25; the icon gets a path without spaces.
      local icon = path.join(installerWork, "vfproj.ico")
      copyFile(launcherIcon, icon)
      local associationFile = path.join(installerWork, "vfproj.properties")
      local written, writeErr = io.writefile(associationFile, table.concat({
         "extension=vfproj",
         "mime-type=application/x-vertexforge-project",
         "description=VertexForge Project",
         "icon=" .. propertiesValue(path.getabsolute(icon)),
         ""
      }, "\n"))
      if not written then
         failInstaller("cannot write " .. associationFile .. ": " .. reason(writeErr))
      end
      local args = {
         "--type", "msi",
         "--app-image", cmdQuote(winPath(staging)),
         "--name", cmdQuote(vfMsiName),
         "--app-version", engineVersion,
         "--vendor", cmdQuote(vfMsiVendor),
         "--description", cmdQuote(vfMsiDescription),
         "--win-per-user-install",
         "--win-upgrade-uuid", vfMsiUpgradeCode,
         "--win-menu",
         "--win-menu-group", cmdQuote(vfMsiMenuGroup),
         "--install-dir", cmdQuote(vfMsiInstallDir),
         "--file-associations", cmdQuote(winPath(associationFile)),
         "--dest", cmdQuote(winPath(installerWork)),
         "--temp", cmdQuote(winPath(path.join(installerWork, "temp"))),
         "--verbose",
      }
      local log = path.join(installerWork, "jpackage.log")
      local built, how, exitCode = os.execute('"' .. cmdQuote(winPath(jdk.jpackage)) .. " " .. table.concat(args, " ") ..
         " > " .. cmdQuote(winPath(log)) .. ' 2>&1"')
      if not built then
         local lines = {}
         for line in (io.readfile(log) or ""):gmatch("[^\r\n]+") do
            table.insert(lines, line)
         end
         print("  jpackage output (the last 40 lines of " .. log .. "):")
         for i = math.max(1, #lines - 39), #lines do
            print("    " .. lines[i])
         end
         failInstaller("jpackage --type msi failed (" .. tostring(how) .. " " .. tostring(exitCode) .. "); its whole " ..
            "output is in " .. log .. ". The package is complete in " .. staging)
      end
      local msis = os.matchfiles(path.join(installerWork, "*.msi"))
      if #msis ~= 1 then
         failInstaller("jpackage --type msi left " .. #msis .. " .msi files in " .. installerWork .. " instead of one")
      end
      builtMsi = msis[1]

      -- What Windows Installer will do, read from the MSI's own tables.
      local report = path.join(installerWork, "msi.json")
      local inspectOutput, inspected = runCaptured(cmdQuote(winPath(powershell)) ..
         " -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " .. cmdQuote(winPath(vfMsiInspectScript)) ..
         " -Msi " .. cmdQuote(winPath(builtMsi)) .. " -Out " .. cmdQuote(winPath(report)))
      local facts = inspected and readJson(report) or nil
      if not facts then
         failInstaller("tools/msi_inspect.ps1 could not read " .. builtMsi .. ": " .. reason(inspectOutput))
      end
      local function badMsi(what)
         failInstaller(builtMsi .. " " .. what)
      end
      local properties = type(facts.properties) == "table" and facts.properties or {}
      -- A GUID as the MSI stores it ("{4E0F9BE5-...}") in the form vfMsiUpgradeCode is written in.
      local function guid(value)
         return (tostring(value or ""):lower():gsub("[{}]", ""))
      end
      if properties.ProductVersion ~= engineVersion then
         badMsi("has ProductVersion " .. tostring(properties.ProductVersion) .. ", not " .. engineVersion)
      end
      if guid(properties.UpgradeCode) ~= vfMsiUpgradeCode then
         badMsi("has UpgradeCode " .. tostring(properties.UpgradeCode) .. ", not the permanent " .. vfMsiUpgradeCode ..
            ": it would not upgrade earlier installs")
      end
      if properties.ProductName ~= vfMsiName or properties.Manufacturer ~= vfMsiVendor then
         badMsi("is " .. tostring(properties.ProductName) .. " by " .. tostring(properties.Manufacturer) .. ", not " ..
            vfMsiName .. " by " .. vfMsiVendor)
      end
      -- Per user: ALLUSERS unset or empty, or the dual-purpose ALLUSERS=2 with MSIINSTALLPERUSER=1.
      local allUsers = properties.ALLUSERS
      if not (allUsers == nil or allUsers == "" or (allUsers == "2" and properties.MSIINSTALLPERUSER == "1")) then
         badMsi("sets ALLUSERS=" .. tostring(allUsers) .. ": it installs for all users and asks for elevation (UAC)")
      end
      local wordCount = tonumber(facts.wordCount) or 0
      if math.floor(wordCount / 8) % 2 ~= 1 then
         badMsi("does not say that it needs no elevation (summary Word Count " .. wordCount .. " lacks bit 8), so " ..
            "Windows would ask for UAC")
      end
      local expectedInstallDir = "LocalAppDataFolder/" .. vfMsiInstallDir:gsub("\\", "/")
      if facts.installDir ~= expectedInstallDir then
         badMsi("installs into " .. tostring(facts.installDir) .. ", not " .. expectedInstallDir)
      end

      local installed = {}
      for _, f in ipairs(type(facts.files) == "table" and facts.files or {}) do
         installed[f:lower()] = f
      end
      local launcherKey = facts.launcherFileKey
      if type(launcherKey) ~= "string" or not installed[launcherExeName:lower()] then
         badMsi("does not install " .. launcherExeName .. " at the top of the install folder")
      end
      if type(facts.outsideFiles) == "table" and #facts.outsideFiles > 0 then
         badMsi("installs " .. #facts.outsideFiles .. " files outside the install folder, e.g. " .. facts.outsideFiles[1])
      end
      local stagedFiles = relativeFiles(staging)
      local missing, stagedLower = {}, {}
      for rel in pairs(stagedFiles) do
         stagedLower[rel:lower()] = true
         if rel ~= "app/.jpackage.xml" and not installed[rel:lower()] then
            table.insert(missing, rel)
         end
      end
      table.sort(missing)
      if #missing > 0 then
         badMsi("does not install " .. missing[1] .. (#missing > 1 and (" (and " .. (#missing - 1) .. " more)") or ""))
      end
      local extras = {}
      for lower, f in pairs(installed) do
         local inner = lower:match("^bin/[^/]+/release/x64/(.+)$")
         if inner and not shippedBinFile(inner) then
            badMsi("installs " .. f .. ", which a package never ships")
         end
         if not stagedLower[lower] and lower ~= "app/.package" and not lower:match("^fa_[^/]*%.ico$") then
            table.insert(extras, f)
         end
      end
      if #extras > 0 then
         table.sort(extras)
         warn(builtMsi .. " also installs " .. #extras .. " files the package does not hold: " ..
            table.concat(extras, ", ", 1, math.min(#extras, 5)))
      end

      local menuFolder = "ProgramMenuFolder/" .. vfMsiMenuGroup
      local shortcuts, described = {}, {}
      for _, s in ipairs(type(facts.shortcuts) == "table" and facts.shortcuts or {}) do
         table.insert(described, tostring(s.folder) .. "/" .. tostring(s.name) .. " -> " .. tostring(s.target))
         if s.folder == menuFolder then
            table.insert(shortcuts, s)
         end
      end
      if #shortcuts ~= 1 or shortcuts[1].name ~= vfMsiName or shortcuts[1].target ~= "[#" .. launcherKey .. "]" then
         badMsi("does not add exactly one Start menu entry " .. vfMsiMenuGroup .. " > " .. vfMsiName .. " that starts " ..
            "the launcher ([#" .. launcherKey .. "]); its shortcuts: " ..
            (#described > 0 and table.concat(described, "; ") or "none"))
      end
      local condition = shortcuts[1].condition
      if type(condition) == "string" and condition ~= "" then
         local property = condition:match("^%s*([%w_]+)%s*$")
         if not property then
            warn(builtMsi .. ": the Start menu entry is installed only when " .. condition)
         elseif properties[property] ~= "1" then
            badMsi("installs its Start menu entry only when " .. property .. " is set, and it is " ..
               tostring(properties[property]))
         end
      end

      local associations = {}
      for _, a in ipairs(type(facts.associations) == "table" and facts.associations or {}) do
         if tostring(a.extension):lower() == "vfproj" then
            table.insert(associations, a)
         end
      end
      if #associations == 0 then
         badMsi("does not associate .vfproj files with the launcher: jpackage " .. jdk.version .. " left " ..
            "--file-associations out, as JDK-8372753 does when packaging an app image (fixed in OpenJDK 25.0.2 and " ..
            "Oracle JDK 25.0.3). Use a JDK 25 with the fix")
      end
      local association = associations[1]
      local launcherRef = ("[#" .. launcherKey .. "]"):gsub("%p", "%%%0")
      if #associations > 1 or not tostring(association.command):find(launcherRef .. '"?%s+"%%1"') then
         badMsi("does not open .vfproj files with the launcher and the file's path ([#" .. launcherKey .. '] "%1"); ' ..
            "its command is " .. tostring(association.command))
      end
      if association.contentType ~= "application/x-vertexforge-project" then
         warn(builtMsi .. ": .vfproj has the content type " .. tostring(association.contentType))
      end
      if type(association.icon) ~= "string" or association.icon == "" then
         warn(builtMsi .. ": .vfproj files get no icon")
      end

      -- Upgrades: a row that removes the installed versions below this one (not detect-only, attribute bit 2).
      local upgradesOlder = false
      for _, u in ipairs(type(facts.upgrade) == "table" and facts.upgrade or {}) do
         if guid(u.upgradeCode) == vfMsiUpgradeCode and u.versionMax == engineVersion and
               math.floor((tonumber(u.attributes) or 0) / 2) % 2 == 0 then
            upgradesOlder = true
         end
      end
      if not upgradesOlder then
         badMsi("has no Upgrade row that removes the installed versions below " .. engineVersion .. ", so it would " ..
            "install beside them instead of upgrading them")
      end

      -- The installer of this version from an earlier run must be replaceable before the package is swapped.
      if os.isfile(msiTarget) then
         os.chmod(msiTarget, "666")
         local handle = io.open(msiTarget, "r+b")
         if not handle then
            failInstaller("cannot replace " .. msiTarget .. ": close any installation running from it and any " ..
               "program using it, then re-run")
         end
         handle:close()
      end
   end

   -- 7. Swap: the old package moves aside in one rename, so an open file inside it fails the
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
   distUnchanged = false
   local removed, removeErr = removeTree(old)
   if not removed then
      warn("the previous package is left in " .. old .. " (" .. removeErr .. "); the next run deletes it")
   end

   -- 8. The installer next to the package; installers of other versions stay.
   local msiBytes
   if buildMsi then
      if os.isfile(msiTarget) then
         local gone, goneErr = os.remove(msiTarget)
         if not gone then
            fail("the new package is in " .. dist .. ", but " .. msiTarget .. " could not be replaced (" ..
               reason(goneErr) .. "); the new installer is " .. builtMsi)
         end
      end
      local placed, placeErr = os.rename(builtMsi, msiTarget)
      if not placed then
         fail("the new package is in " .. dist .. ", but the new installer could not be moved to " .. msiTarget ..
            " (" .. reason(placeErr) .. "); it is " .. builtMsi)
      end
      local stat = os.stat(msiTarget)
      msiBytes = stat and stat.size or 0
      local cleaned, cleanErr = removeTree(installerWork)
      if not cleaned then
         warn(installerWork .. " is left behind (" .. cleanErr .. "); the next run deletes it")
      end
   end

   -- 9. Summary.
   local function describe(stats)
      return string.format("%d %s, %s", stats.files, stats.files == 1 and "file" or "files", describeBytes(stats.bytes))
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
   if buildMsi then
      print(string.format("  %-32s %s  jpackage %s, WiX %s", path.getname(msiTarget), describeBytes(msiBytes),
         jdk.version, wix.version))
      print("    Installs for the current user only (no UAC) into %LOCALAPPDATA%\\" .. vfMsiInstallDir ..
         ", adds the Start menu entry " .. vfMsiMenuGroup .. " > " .. vfMsiName .. ", and opens .vfproj files in the launcher.")
      print("    Uninstalling AND upgrading delete the whole install folder, with what the editor wrote there (logs/,")
      print("    crashes/, cache/, imgui.ini, plugins/data/, Plugin Manager choices, plugins created under plugins/);")
      print("    ~/.vertexforge and the projects are kept. Close the launcher and every editor before installing,")
      print("    upgrading or uninstalling. This version installs again only after an uninstall (MSI error 1638).")
   else
      print(string.format("  %-32s %s", "installer", skipLauncher and "SKIPPED (--skip-launcher)" or "SKIPPED (--skip-msi)"))
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
      print("Run: \"" .. dist .. "/" .. launcherExeName .. "\"")
   end
   if buildMsi then
      print("Install: \"" .. msiTarget .. "\"")
   end
end

newaction {
   trigger = "package-release",
   description = "Assemble the single-folder ship distribution (Editor+Runtime+SDK+resources+Tests+plugins+engine content+launcher) into dist/VertexForge/, and its per-user MSI installer",
   execute = vfPackageRelease
}
