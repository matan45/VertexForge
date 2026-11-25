# tools/authenticode_inspect.ps1 -Root <folder> | -File <file> | -Certificate <thumbprint> [-MachineStore]
#
# VK-1696: reads Authenticode signatures and exe version resources READ-ONLY for tools/package_release.lua, and prints
# them as ONE line of JSON on stdout. Every character outside ASCII is written as a \uXXXX escape, so the line
# survives any console code page; the script writes no file, so package-release can run it during its preflight.
# Windows PowerShell 5.1 only; run it by hand on any folder or file to see the same facts.
#
#   -Root <folder>   { files: [ entry... ] } for every file below <folder> that is a PE image ("MZ", then "PE\0\0" at
#                    e_lfanew), whatever its extension; path = relative to <folder>, "/"-joined, sorted.
#   -File <file>     { files: [ entry ] } for that one file (an MSI, say), whether it is a PE image or not.
#   -Certificate <t> { found, subject, thumbprint, store, hasPrivateKey, codeSigning, notBefore, notAfter, valid,
#                    chainTrusted, chainStatus } for the certificate with SHA-1 thumbprint <t> in CurrentUser\My
#                    (LocalMachine\My with -MachineStore). chainTrusted: the chain ends in a trusted root, checked
#                    without revocation (an offline machine must not fail it). signtool needs the private key and,
#                    when the certificate lists enhanced key usages, Code Signing (1.3.6.1.5.5.7.3.3) among them.
#
# An entry: { path, status, statusMessage, signatureType, signerSubject, signerThumbprint, timestamped,
#             timestampSubject, version }
#   status         Get-AuthenticodeSignature's Status: Valid, NotSigned, HashMismatch, NotTrusted, UnknownError, ...
#                  It uses the same WinVerifyTrust policy as `signtool verify /pa`.
#   signatureType  Authenticode (embedded in the file: travels with it), Catalog (Valid only because THIS machine's
#                  catalog lists the file), None.
#   version        for an .exe: { companyName, productName, productVersion, fileVersion, fileDescription,
#                  internalName, originalFilename, fileMajor, fileMinor, fileBuild, filePrivate } (the numbers are
#                  the fixed FILEVERSION parts); null for other files.
#
# Exit code 0 when the JSON was printed, 1 (with the reason on stderr) otherwise.

[CmdletBinding(DefaultParameterSetName = 'Root')]
param(
    [Parameter(ParameterSetName = 'Root', Mandatory = $true)][string]$Root,
    [Parameter(ParameterSetName = 'File', Mandatory = $true)][string]$File,
    [Parameter(ParameterSetName = 'Certificate', Mandatory = $true)][string]$Certificate,
    [Parameter(ParameterSetName = 'Certificate')][switch]$MachineStore
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$WarningPreference = 'SilentlyContinue'

# True when the file starts with "MZ" and holds "PE\0\0" where its e_lfanew points. Opened for reading only, sharing
# read, write and delete, so an open file is not disturbed.
function Test-PeImage([string]$Path) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    try {
        $header = New-Object byte[] 64
        if ($stream.Read($header, 0, 64) -lt 64 -or $header[0] -ne 0x4D -or $header[1] -ne 0x5A) {
            return $false
        }
        $lfanew = [BitConverter]::ToUInt32($header, 0x3C)
        if ([int64]$lfanew + 4 -gt $stream.Length) {
            return $false
        }
        [void]$stream.Seek($lfanew, [IO.SeekOrigin]::Begin)
        $signature = New-Object byte[] 4
        if ($stream.Read($signature, 0, 4) -lt 4) {
            return $false
        }
        return $signature[0] -eq 0x50 -and $signature[1] -eq 0x45 -and $signature[2] -eq 0 -and $signature[3] -eq 0
    }
    finally {
        $stream.Dispose()
    }
}

function Get-VersionFacts([string]$Path) {
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
    return [ordered]@{
        companyName      = $info.CompanyName
        productName      = $info.ProductName
        productVersion   = $info.ProductVersion
        fileVersion      = $info.FileVersion
        fileDescription  = $info.FileDescription
        internalName     = $info.InternalName
        originalFilename = $info.OriginalFilename
        fileMajor        = $info.FileMajorPart
        fileMinor        = $info.FileMinorPart
        fileBuild        = $info.FileBuildPart
        filePrivate      = $info.FilePrivatePart
    }
}

function Get-FileFacts([string]$Path, [string]$Relative) {
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    $signer = $signature.SignerCertificate
    $stamper = $signature.TimeStamperCertificate
    $version = $null
    if ([IO.Path]::GetExtension($Path) -ieq '.exe') {
        $version = Get-VersionFacts $Path
    }
    return [ordered]@{
        path             = $Relative
        status           = [string]$signature.Status
        statusMessage    = [string]$signature.StatusMessage
        signatureType    = [string]$signature.SignatureType
        signerSubject    = $(if ($null -ne $signer) { $signer.Subject } else { $null })
        signerThumbprint = $(if ($null -ne $signer) { $signer.Thumbprint } else { $null })
        timestamped      = $null -ne $stamper
        timestampSubject = $(if ($null -ne $stamper) { $stamper.Subject } else { $null })
        version          = $version
    }
}

function Get-CertificateFacts([string]$Thumbprint, [bool]$Machine) {
    $location = $(if ($Machine) { 'LocalMachine' } else { 'CurrentUser' })
    $result = [ordered]@{
        found         = $false
        subject       = $null
        thumbprint    = $Thumbprint
        store         = "$location\My"
        hasPrivateKey = $false
        codeSigning   = $false
        notBefore     = $null
        notAfter      = $null
        valid         = $false
        chainTrusted  = $false
        chainStatus   = @()
    }
    $certificate = Get-Item -LiteralPath "Cert:\$location\My\$Thumbprint" -ErrorAction SilentlyContinue
    if ($null -eq $certificate) {
        return $result
    }
    $result.found = $true
    $result.subject = $certificate.Subject
    $result.thumbprint = $certificate.Thumbprint
    $result.hasPrivateKey = [bool]$certificate.HasPrivateKey
    $usages = @($certificate.Extensions | Where-Object {
            $_ -is [Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension] })
    if ($usages.Count -eq 0) {
        $result.codeSigning = $true # no EKU extension: valid for every usage
    }
    else {
        foreach ($usage in $usages) {
            foreach ($oid in $usage.EnhancedKeyUsages) {
                if ($oid.Value -eq '1.3.6.1.5.5.7.3.3') {
                    $result.codeSigning = $true
                }
            }
        }
    }
    $result.notBefore = $certificate.NotBefore.ToUniversalTime().ToString('o')
    $result.notAfter = $certificate.NotAfter.ToUniversalTime().ToString('o')
    $now = Get-Date
    $result.valid = $certificate.NotBefore -le $now -and $now -le $certificate.NotAfter

    $chain = New-Object Security.Cryptography.X509Certificates.X509Chain
    try {
        $chain.ChainPolicy.RevocationMode = [Security.Cryptography.X509Certificates.X509RevocationMode]::NoCheck
        $result.chainTrusted = [bool]$chain.Build($certificate)
        $result.chainStatus = @($chain.ChainStatus | ForEach-Object { [string]$_.Status })
    }
    finally {
        $chain.Dispose()
    }
    return $result
}

# JSON with every non-ASCII UTF-16 code unit as \uXXXX (a surrogate pair becomes two escapes, which JSON allows).
function ConvertTo-AsciiJson($Value) {
    $json = ConvertTo-Json -InputObject $Value -Depth 6 -Compress
    $builder = New-Object Text.StringBuilder
    foreach ($character in $json.ToCharArray()) {
        if ([int]$character -gt 0x7E) {
            [void]$builder.AppendFormat('\u{0:x4}', [int]$character)
        }
        else {
            [void]$builder.Append($character)
        }
    }
    return $builder.ToString()
}

try {
    switch ($PSCmdlet.ParameterSetName) {
        'Root' {
            $rootPath = (Resolve-Path -LiteralPath $Root).ProviderPath.TrimEnd('\')
            $entries = New-Object System.Collections.ArrayList
            $files = Get-ChildItem -LiteralPath $rootPath -Recurse -File -Force | Sort-Object FullName
            foreach ($item in $files) {
                if (Test-PeImage $item.FullName) {
                    $relative = $item.FullName.Substring($rootPath.Length + 1).Replace('\', '/')
                    [void]$entries.Add((Get-FileFacts $item.FullName $relative))
                }
            }
            $result = [ordered]@{ files = @($entries) }
        }
        'File' {
            $filePath = (Resolve-Path -LiteralPath $File).ProviderPath
            $result = [ordered]@{ files = @(Get-FileFacts $filePath ([IO.Path]::GetFileName($filePath))) }
        }
        'Certificate' {
            $result = Get-CertificateFacts $Certificate $MachineStore.IsPresent
        }
    }
    [Console]::Out.Write((ConvertTo-AsciiJson $result))
    exit 0
}
catch {
    [Console]::Error.WriteLine("authenticode_inspect: " + $_.Exception.Message)
    exit 1
}
