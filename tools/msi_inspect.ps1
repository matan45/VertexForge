# tools/msi_inspect.ps1 -Msi <file.msi> -Out <file.json>
#
# VK-1693: reads an MSI database READ-ONLY and writes the facts tools/package_release.lua checks about the installer
# jpackage built, as UTF-8 JSON without a BOM. It reads the database itself, not jpackage's WiX sources, so it sees
# what Windows Installer will do. Windows PowerShell 5.1 and the WindowsInstaller.Installer COM object only; run it by
# hand on any MSI to see the same facts.
#
#   properties     every Property row (name -> value)
#   wordCount      the summary information Word Count (PID 15); bit 8 = "elevated privileges are not required"
#   installDir     INSTALLDIR's folder chain below TARGETDIR, "/"-joined (standard folders by their Id)
#   files          every installed file relative to INSTALLDIR, "/"-joined and sorted; outsideFiles: the others
#   launcherFileKey the File key of "VertexForge Launcher.exe" directly in INSTALLDIR (null when absent)
#   shortcuts      { folder, name, target, condition } per Shortcut row (folder = the chain below TARGETDIR)
#   associations   { extension, progId, command, label, icon, contentType } per extension registered in the classes
#                  root (Registry table: HKCR, or HKCU\Software\Classes)
#   extensionTable { extension, progId, mime, verbs } per row of the (advertised) Extension table, for diagnostics
#   upgrade        { upgradeCode, versionMin, versionMax, attributes, actionProperty } per Upgrade row
#
# Exit code 0 when the JSON was written, 1 (with the reason on stderr) otherwise. Every COM object is released before
# the script exits, so the MSI file is not left locked.

param(
    [Parameter(Mandatory = $true)][string]$Msi,
    [Parameter(Mandatory = $true)][string]$Out
)

$ErrorActionPreference = 'Stop'

$released = New-Object System.Collections.ArrayList

# The WindowsInstaller objects carry no type information PowerShell can bind to, so every call goes through
# InvokeMember.
function Invoke-MsiMethod($Object, [string]$Name, [object[]]$Arguments) {
    return $Object.GetType().InvokeMember($Name, [Reflection.BindingFlags]::InvokeMethod, $null, $Object, $Arguments)
}

function Get-MsiValue($Object, [string]$Name, [object[]]$Arguments) {
    return $Object.GetType().InvokeMember($Name, [Reflection.BindingFlags]::GetProperty, $null, $Object, $Arguments)
}

function Release-Com($Object) {
    if ($null -ne $Object -and [Runtime.InteropServices.Marshal]::IsComObject($Object)) {
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($Object)
    }
}

# Every row of a query, each as a string[] of $Columns values (a NULL column reads as "").
function Read-Rows($Database, [string]$Sql, [int]$Columns) {
    $rows = New-Object System.Collections.ArrayList
    $view = Invoke-MsiMethod $Database 'OpenView' @($Sql)
    try {
        [void](Invoke-MsiMethod $view 'Execute' $null)
        while ($true) {
            $record = Invoke-MsiMethod $view 'Fetch' $null
            if ($null -eq $record) {
                break
            }
            try {
                $values = New-Object 'string[]' $Columns
                for ($i = 1; $i -le $Columns; $i++) {
                    $values[$i - 1] = [string](Get-MsiValue $record 'StringData' @($i))
                }
                [void]$rows.Add($values)
            } finally {
                Release-Com $record
            }
        }
        [void](Invoke-MsiMethod $view 'Close' $null)
    } finally {
        Release-Com $view
    }
    return ,$rows
}

# The long name of a file or folder: "SHORT~1|Long name" -> "Long name".
function Get-LongName([string]$Name) {
    $bar = $Name.LastIndexOf('|')
    if ($bar -ge 0) {
        return $Name.Substring($bar + 1)
    }
    return $Name
}

$standardFolders = @(
    'TARGETDIR', 'LocalAppDataFolder', 'AppDataFolder', 'ProgramMenuFolder', 'StartMenuFolder', 'DesktopFolder',
    'ProgramFilesFolder', 'ProgramFiles64Folder', 'CommonFilesFolder', 'CommonFiles64Folder', 'PersonalFolder',
    'StartupFolder', 'WindowsFolder', 'SystemFolder', 'System64Folder', 'TempFolder', 'CommonAppDataFolder')

$installer = $null
$database = $null
$summary = $null
$code = 1
try {
    $msiPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Msi)
    $outPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)
    if (-not (Test-Path -LiteralPath $msiPath -PathType Leaf)) {
        throw "no such file: $msiPath"
    }

    $installer = New-Object -ComObject WindowsInstaller.Installer
    # 0 = msiOpenDatabaseModeReadOnly.
    $database = Invoke-MsiMethod $installer 'OpenDatabase' @($msiPath, 0)

    $tables = @{}
    foreach ($row in (Read-Rows $database 'SELECT `Name` FROM `_Tables`' 1)) {
        $tables[$row[0]] = $true
    }

    $properties = [ordered]@{}
    if ($tables.ContainsKey('Property')) {
        foreach ($row in (Read-Rows $database 'SELECT `Property`, `Value` FROM `Property`' 2)) {
            $properties[$row[0]] = $row[1]
        }
    }

    $summary = Get-MsiValue $database 'SummaryInformation' @(0)
    $wordCount = [int](Get-MsiValue $summary 'Property' @(15))

    # Directory id -> { parent, name } (name = the target long name; "" for "." and for standard folders' own Id).
    $directories = @{}
    if ($tables.ContainsKey('Directory')) {
        foreach ($row in (Read-Rows $database 'SELECT `Directory`, `Directory_Parent`, `DefaultDir` FROM `Directory`' 3)) {
            $target = $row[2]
            $colon = $target.IndexOf(':')
            if ($colon -ge 0) {
                $target = $target.Substring(0, $colon)
            }
            $name = Get-LongName $target
            if ($standardFolders -contains $row[0]) {
                $name = $row[0]
            } elseif ($name -eq '.') {
                $name = ''
            }
            $directories[$row[0]] = @{ parent = $row[1]; name = $name }
        }
    }

    # The folder names from just below $Stop down to $Id ("/"-joined), or $null when the chain never reaches $Stop.
    function Get-Chain([string]$Id, [string]$Stop) {
        $names = New-Object System.Collections.ArrayList
        $current = $Id
        $guard = 0
        while ($current -ne $Stop) {
            if ([string]::IsNullOrEmpty($current) -or -not $directories.ContainsKey($current) -or $guard -gt 256) {
                return $null
            }
            $entry = $directories[$current]
            if ($entry.name -ne '') {
                [void]$names.Insert(0, $entry.name)
            }
            # A root directory is its own parent or has none.
            if ($entry.parent -eq $current) {
                $current = ''
            } else {
                $current = $entry.parent
            }
            $guard++
        }
        return ($names -join '/')
    }

    $installDir = $null
    if ($directories.ContainsKey('INSTALLDIR')) {
        $installDir = Get-Chain 'INSTALLDIR' 'TARGETDIR'
    }

    $components = @{}
    if ($tables.ContainsKey('Component')) {
        foreach ($row in (Read-Rows $database 'SELECT `Component`, `Directory_`, `Condition` FROM `Component`' 3)) {
            $components[$row[0]] = @{ directory = $row[1]; condition = $row[2] }
        }
    }

    $files = New-Object System.Collections.ArrayList
    $outsideFiles = New-Object System.Collections.ArrayList
    $launcherFileKey = $null
    if ($tables.ContainsKey('File')) {
        foreach ($row in (Read-Rows $database 'SELECT `File`, `Component_`, `FileName` FROM `File`' 3)) {
            $name = Get-LongName $row[2]
            $component = $components[$row[1]]
            $folder = $null
            if ($null -ne $component) {
                $folder = Get-Chain $component.directory 'INSTALLDIR'
            }
            if ($null -eq $folder) {
                [void]$outsideFiles.Add($name)
                continue
            }
            if ($folder -eq '') {
                $relative = $name
            } else {
                $relative = $folder + '/' + $name
            }
            [void]$files.Add($relative)
            if ($relative -eq 'VertexForge Launcher.exe') {
                $launcherFileKey = $row[0]
            }
        }
    }
    $sortedFiles = @($files | Sort-Object)

    $shortcuts = New-Object System.Collections.ArrayList
    if ($tables.ContainsKey('Shortcut')) {
        foreach ($row in (Read-Rows $database 'SELECT `Directory_`, `Name`, `Component_`, `Target` FROM `Shortcut`' 4)) {
            $condition = ''
            if ($components.ContainsKey($row[2])) {
                $condition = $components[$row[2]].condition
            }
            [void]$shortcuts.Add([ordered]@{
                folder = (Get-Chain $row[0] 'TARGETDIR')
                name = (Get-LongName $row[1])
                target = $row[3]
                condition = $condition
            })
        }
    }

    # The classes root: HKCR (Root 0) as is, HKCU/HKLM (1/2) and the per-user-or-machine root (-1) below
    # Software\Classes. Keys are case-insensitive; "" names the default value.
    $classes = @{}
    if ($tables.ContainsKey('Registry')) {
        foreach ($row in (Read-Rows $database 'SELECT `Root`, `Key`, `Name`, `Value` FROM `Registry`' 4)) {
            $key = $row[1]
            if ($row[0] -ne '0') {
                $prefix = 'Software\Classes\'
                if ($key.Length -le $prefix.Length -or
                        -not $key.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
                    continue
                }
                $key = $key.Substring($prefix.Length)
            }
            $classes[($key.ToLowerInvariant() + '|' + $row[2].ToLowerInvariant())] = $row[3]
        }
    }

    function Get-ClassValue([string]$Key, [string]$Name) {
        $lookup = $Key.ToLowerInvariant() + '|' + $Name.ToLowerInvariant()
        if ($classes.ContainsKey($lookup)) {
            return $classes[$lookup]
        }
        return $null
    }

    $associations = New-Object System.Collections.ArrayList
    foreach ($entry in @($classes.Keys | Sort-Object)) {
        $parts = $entry.Split('|')
        if ($parts[1] -ne '' -or -not $parts[0].StartsWith('.') -or $parts[0].Contains('\')) {
            continue
        }
        $extensionKey = $parts[0]
        $progId = $classes[$entry]
        [void]$associations.Add([ordered]@{
            extension = $extensionKey.Substring(1)
            progId = $progId
            command = (Get-ClassValue ($progId + '\shell\open\command') '')
            label = (Get-ClassValue ($progId + '\shell\open') '')
            icon = (Get-ClassValue ($progId + '\DefaultIcon') '')
            contentType = (Get-ClassValue $extensionKey 'Content Type')
        })
    }

    $extensionTable = New-Object System.Collections.ArrayList
    if ($tables.ContainsKey('Extension')) {
        $verbs = @{}
        if ($tables.ContainsKey('Verb')) {
            foreach ($row in (Read-Rows $database 'SELECT `Extension_`, `Verb`, `Argument` FROM `Verb`' 3)) {
                $verbs[$row[0]] = @($verbs[$row[0]]) + @($row[1] + ' ' + $row[2])
            }
        }
        foreach ($row in (Read-Rows $database 'SELECT `Extension`, `ProgId_`, `MIME_` FROM `Extension`' 3)) {
            [void]$extensionTable.Add([ordered]@{
                extension = $row[0]
                progId = $row[1]
                mime = $row[2]
                verbs = @($verbs[$row[0]] | Where-Object { $null -ne $_ })
            })
        }
    }

    $upgrade = New-Object System.Collections.ArrayList
    if ($tables.ContainsKey('Upgrade')) {
        foreach ($row in (Read-Rows $database 'SELECT `UpgradeCode`, `VersionMin`, `VersionMax`, `Attributes`, `ActionProperty` FROM `Upgrade`' 5)) {
            [void]$upgrade.Add([ordered]@{
                upgradeCode = $row[0]
                versionMin = $row[1]
                versionMax = $row[2]
                attributes = $row[3]
                actionProperty = $row[4]
            })
        }
    }

    $result = [ordered]@{
        properties = $properties
        wordCount = $wordCount
        installDir = $installDir
        files = $sortedFiles
        outsideFiles = @($outsideFiles)
        launcherFileKey = $launcherFileKey
        shortcuts = @($shortcuts)
        associations = @($associations)
        extensionTable = @($extensionTable)
        upgrade = @($upgrade)
    }
    $json = ConvertTo-Json -InputObject $result -Depth 6
    [IO.File]::WriteAllText($outPath, $json, (New-Object Text.UTF8Encoding $false))
    $code = 0
} catch {
    [Console]::Error.WriteLine("msi_inspect: $($_.Exception.Message)")
    $code = 1
} finally {
    Release-Com $summary
    Release-Com $database
    Release-Com $installer
    [GC]::Collect()
    [GC]::WaitForPendingFinalizers()
}
exit $code
