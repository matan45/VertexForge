-- Engine identity (VK-1680): one version source and the engine.json manifest.
--
--   VERSION (repo root, MAJOR.MINOR.PATCH) is the ONLY engine (product) version source:
--     vfWriteEngineVersionHeader() turns it into VFEngine/utilities/config/EngineVersion.generated.hpp
--     on every premake run (root premake5.lua), and Config.hpp's `struct Version` reads that header.
--     The generated file is gitignored; its sdk/include copy (export_sdk.lua) is tracked.
--     The binary asset format has its own version (AssetFormatVersion in Config.hpp), so a VERSION
--     bump never forces a re-import.
--
--   engine.json sits beside Editor.exe so the launcher can identify an install without running it:
--     dev:     the Editor postbuild runs `premake5 --file=tools/engine_version.lua --vfconfig=<Config> engine-manifest`
--     release: tools/package_release.lua rewrites it for dist/VertexForge (vfWriteEngineManifest)
--   Every path in it is relative to the manifest's own folder.
--
-- This file is loaded two ways: include()d by the root premake5.lua, and standalone (--file) by the
-- Editor postbuild. Premake changes into a script's folder while loading it and the postbuild runs
-- from VFEngine/editor, so every path below is absolute, built from this file's own folder.

local repoRoot = path.getdirectory(_SCRIPT_DIR)

local generatedHeaderRel = "VFEngine/utilities/config/EngineVersion.generated.hpp"
local engineManifestVersion = 1
-- Engine content the launcher copies from, relative to the content root (the repo root in the dev
-- tree, dist/VertexForge in the release bundle, which mirrors the repo layout).
local templatesRel = "assets/templates"
local scriptLibRel = "assets/scripts/lib"

local function repoPath(rel)
   return path.join(repoRoot, rel)
end

local function readRepoFile(rel)
   local text = io.readfile(repoPath(rel))
   if not text then
      error("engine_version.lua: cannot read " .. repoPath(rel), 0)
   end
   return text
end

local function writeFileIfChanged(text, file)
   local result, err = os.writefile_ifnotequal(text, file)
   if result == nil or result < 0 then
      error("engine_version.lua: cannot write " .. file .. ": " .. tostring(err), 0)
   end
   return result > 0
end

-- The value N of the first "uint32_t <name> = N;" after the anchor pattern in a repo file.
local function uintAfter(rel, anchor, name)
   local src = readRepoFile(rel)
   local at = src:find(anchor)
   local value = at and src:match("uint32_t%s+" .. name .. "%s*=%s*(%d+)%s*;", at)
   if not value then
      error("engine_version.lua: '" .. name .. "' not found after '" .. anchor .. "' in " .. rel, 0)
   end
   return value
end

-- The engine version from the repo-root VERSION file: { major, minor, patch, text } (strings).
-- Strict MAJOR.MINOR.PATCH; a UTF-8 BOM and surrounding whitespace (CRLF) are ignored.
function vfReadEngineVersion()
   local raw = readRepoFile("VERSION")
   local text = (raw:gsub("^\239\187\191", "")):match("^%s*(.-)%s*$")
   local major, minor, patch = text:match("^(%d+)%.(%d+)%.(%d+)$")
   if not major then
      error("VERSION must be MAJOR.MINOR.PATCH (e.g. 1.0.0), got '" .. text .. "'", 0)
   end
   for _, part in ipairs({ major, minor, patch }) do
      if #part > 1 and part:sub(1, 1) == "0" then
         error("VERSION must not have leading zeros, got '" .. text .. "'", 0)
      end
      if #part > 10 or tonumber(part) > 4294967295 then
         error("VERSION numbers must fit in 32 bits, got '" .. text .. "'", 0)
      end
   end
   return { major = major, minor = minor, patch = patch, text = text }
end

-- "X.Y.Z" of `struct AssetFormatVersion` in Config.hpp.
function vfReadAssetFormatVersion()
   local file = "VFEngine/utilities/config/Config.hpp"
   local anchor = "struct%s+AssetFormatVersion%f[^%w_]"
   return uintAfter(file, anchor, "major") .. "." .. uintAfter(file, anchor, "minor") .. "." ..
      uintAfter(file, anchor, "patch")
end

-- "X.Y" of `config::ProjectSchemaVersion` in ProjectConfig.hpp (the .vfproj schemaVersion).
function vfReadProjectSchemaVersion()
   local file = "VFEngine/utilities/config/ProjectConfig.hpp"
   local anchor = "struct%s+ProjectSchemaVersion%f[^%w_]"
   return uintAfter(file, anchor, "major") .. "." .. uintAfter(file, anchor, "minor")
end

-- plugin::VF_PLUGIN_API_VERSION (decimal string).
function vfReadPluginApiVersion()
   local value = readRepoFile("VFEngine/plugin/api/PluginVersion.hpp"):match("VF_PLUGIN_API_VERSION%s*=%s*(%d+)")
   if not value then
      error("engine_version.lua: VF_PLUGIN_API_VERSION not found in VFEngine/plugin/api/PluginVersion.hpp", 0)
   end
   return value
end

-- Writes EngineVersion.generated.hpp from VERSION, only when its content changes (no rebuild storm).
function vfWriteEngineVersionHeader()
   local v = vfReadEngineVersion()
   local text = table.concat({
      "// GENERATED from the repo-root VERSION file by tools/engine_version.lua. Do not edit:",
      "// change VERSION, then re-run premake5 vs2022 (which also refreshes the sdk/include copy).",
      "#pragma once",
      "",
      "#define VF_ENGINE_VERSION_MAJOR " .. v.major,
      "#define VF_ENGINE_VERSION_MINOR " .. v.minor,
      "#define VF_ENGINE_VERSION_PATCH " .. v.patch,
      "#define VF_ENGINE_VERSION_STRING \"" .. v.text .. "\"",
      ""
   }, "\r\n")
   if writeFileIfChanged(text, repoPath(generatedHeaderRel)) then
      print("Generated " .. generatedHeaderRel .. " (engine version " .. v.text .. ")")
   end
end

-- The engine version the sources compile with right now (the generated header).
local function generatedEngineVersion()
   local src = io.readfile(repoPath(generatedHeaderRel))
   local text = src and src:match("VF_ENGINE_VERSION_STRING%s+\"([^\"]+)\"")
   if not text then
      error(generatedHeaderRel .. " is missing: run premake5 vs2022 from the repo root", 0)
   end
   return text
end

-- HEAD's full commit hash, or nil when git or the repository is unavailable.
function vfGitCommit()
   local pipe = io.popen('git -C "' .. repoRoot .. '" rev-parse HEAD 2>nul')
   if not pipe then
      return nil
   end
   local out = pipe:read("*a") or ""
   pipe:close()
   out = out:gsub("%s+$", "")
   if #out == 40 and out:match("^%x+$") then
      return out:lower()
   end
   return nil
end

local function jsonString(s)
   if s:find("%c") then
      error("engine_version.lua: control character in manifest value '" .. s .. "'", 0)
   end
   local escaped = s:gsub("[\\\"]", "\\%0")
   return "\"" .. escaped .. "\""
end

-- Writes <m.exeDir>/engine.json (only when its content changes) and returns its path.
--   m.exeDir        folder of Editor.exe (absolute)
--   m.workingDir    folder the editor must be started in (absolute)
--   m.contentRoot   folder holding assets/ (absolute)
--   m.configuration Debug | Development | Release
--   m.gitCommit     40-hex commit or nil (written as null)
-- Field order is fixed; paths are relative to exeDir with forward slashes; UTF-8, no BOM, LF.
-- Paths are written even when the folder does not exist yet: consumers check.
function vfWriteEngineManifest(m)
   local exeDir = path.getabsolute(m.exeDir)
   local function relative(target)
      local rel = path.getrelative(exeDir, path.getabsolute(target))
      if rel == "" then
         rel = "."
      end
      return rel
   end

   local fields = {
      { "manifestVersion",      tostring(engineManifestVersion) },
      { "engineVersion",        jsonString(vfReadEngineVersion().text) },
      { "assetFormatVersion",   jsonString(vfReadAssetFormatVersion()) },
      { "projectSchemaVersion", jsonString(vfReadProjectSchemaVersion()) },
      { "pluginApiVersion",     vfReadPluginApiVersion() },
      { "configuration",        jsonString(m.configuration) },
      { "gitCommit",            m.gitCommit and jsonString(m.gitCommit) or "null" },
      { "editorExe",            jsonString("Editor.exe") },
      { "workingDirectory",     jsonString(relative(m.workingDir)) },
      { "templatesDir",         jsonString(relative(path.join(m.contentRoot, templatesRel))) },
      { "scriptLibDir",         jsonString(relative(path.join(m.contentRoot, scriptLibRel))) },
   }
   local lines = {}
   for i, field in ipairs(fields) do
      lines[i] = "  \"" .. field[1] .. "\": " .. field[2] .. (i < #fields and "," or "")
   end

   os.mkdir(exeDir)
   local out = path.join(exeDir, "engine.json")
   if writeFileIfChanged("{\n" .. table.concat(lines, "\n") .. "\n}\n", out) then
      print("Wrote " .. out)
   else
      print("Up to date: " .. out)
   end
   return out
end

newoption {
   trigger = "vfconfig",
   value = "CONFIG",
   description = "Editor configuration whose engine.json the engine-manifest action writes",
   allowed = {
      { "Debug", "Debug" },
      { "Development", "Development" },
      { "Release", "Release" }
   }
}

newaction {
   trigger = "engine-manifest",
   description = "Write bin/Editor/<vfconfig>/x64/engine.json for the dev tree (run by the Editor postbuild)",
   execute = function()
      local configuration = _OPTIONS["vfconfig"]
      if not configuration then
         error("engine-manifest needs --vfconfig=<Debug|Development|Release>", 0)
      end
      local version = vfReadEngineVersion().text
      local compiled = generatedEngineVersion()
      if version ~= compiled then
         error("VERSION says " .. version .. " but the Editor was compiled with " .. compiled ..
            ": re-run premake5 vs2022 and rebuild", 0)
      end
      vfWriteEngineManifest {
         exeDir = path.join(repoRoot, "bin/Editor", configuration, "x64"),
         workingDir = path.join(repoRoot, "VFEngine/editor"),
         contentRoot = repoRoot,
         configuration = configuration,
         gitCommit = vfGitCommit()
      }
   end
}
