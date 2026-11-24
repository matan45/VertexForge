# bin\Uninstall.ps1 of an installed VertexForge (VK-1696), run by "Uninstall VertexForge.cmd" at the root of the install
# folder. It starts Windows Installer's own uninstall (msiexec /x) of the per-user VertexForge MSI whose install folder
# is the folder this script was shipped in, so Windows' record of the install goes too (deleting the folder by hand
# leaves it, and the same version's MSI then no longer installs). The product is found by the MSI's permanent upgrade
# code (vfMsiUpgradeCode in tools/package_release.lua), so the script works for every version. A copy that was never
# installed (dist\VertexForge) is refused: delete such a folder instead.
#
#   -Folder <dir>   the VertexForge folder to uninstall (default: the folder above this script)
#   -Check          only say what would be uninstalled
#
# Windows PowerShell 5.1. msiexec runs from %SystemRoot%, so no process of this script keeps the install folder open
# while Windows Installer deletes it.

param(
    [string]$Folder,
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$upgradeCode = '{4E0F9BE5-20F3-4617-BAFD-1E0A49993C67}'

function Close-WithMessage([string]$Message) {
    Write-Host $Message
    Write-Host ''
    if (-not $Check) {
        [void](Read-Host 'Press Enter to close')
    }
    exit 1
}

# The WindowsInstaller objects carry no type information PowerShell can bind to, so every call goes through
# InvokeMember (as in tools/msi_inspect.ps1).
function Get-MsiValue($Object, [string]$Name, [object[]]$Arguments) {
    return $Object.GetType().InvokeMember($Name, [Reflection.BindingFlags]::GetProperty, $null, $Object, $Arguments)
}

if ([string]::IsNullOrEmpty($Folder)) {
    $Folder = Join-Path $PSScriptRoot '..'
}

$product = $null
$version = $null
try {
    $root = [IO.Path]::GetFullPath($Folder).TrimEnd('\')
    $installer = New-Object -ComObject WindowsInstaller.Installer
    # A StringList: PowerShell enumerates it on the way out of the function, so @() collects the product codes (none,
    # one or several) as an array.
    $related = @(Get-MsiValue $installer 'RelatedProducts' @($upgradeCode))
    foreach ($entry in $related) {
        $code = [string]$entry
        $location = ''
        try {
            $location = [string](Get-MsiValue $installer 'ProductInfo' @($code, 'InstallLocation'))
        }
        catch {
            $location = ''
        }
        if ($location -ne '' -and [IO.Path]::GetFullPath($location).TrimEnd('\') -ieq $root) {
            $product = $code
            $version = [string](Get-MsiValue $installer 'ProductInfo' @($code, 'VersionString'))
        }
    }
}
catch {
    Close-WithMessage ('Cannot read the installed programs: ' + $_.Exception.Message)
}

if ($null -eq $product) {
    Close-WithMessage ("$root is not an installed VertexForge. A copy that was never installed (such as " +
        'dist\VertexForge) is removed by deleting its folder; an installed VertexForge is also removed from ' +
        'Settings > Apps > Installed apps > VertexForge Launcher.')
}

if ($Check) {
    Write-Host "Would uninstall VertexForge $version ($product) from $root"
    exit 0
}

Write-Host "Uninstalling VertexForge $version from $root ..."
Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\msiexec.exe') -ArgumentList '/x', $product `
    -WorkingDirectory $env:SystemRoot
exit 0
