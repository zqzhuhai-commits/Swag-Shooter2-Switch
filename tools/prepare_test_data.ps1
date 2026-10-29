param(
    [Parameter(Mandatory=$true)][string]$Apk,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "..\switch\swag2-test")
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Apk))
try {
    $entry = $archive.GetEntry("lib/armeabi-v7a/libil2cpp.so")
    if ($null -eq $entry) { throw "ARMv7 libil2cpp.so was not found in the APK." }
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    $destination = Join-Path (Resolve-Path -LiteralPath $OutputDirectory) "libil2cpp.so"
    [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $destination, $true)
    $header = New-Object byte[] 20
    $stream = [System.IO.File]::OpenRead($destination)
    try { $count = $stream.Read($header, 0, 20) } finally { $stream.Dispose() }
    if ($count -ne 20 -or $header[0] -ne 127 -or $header[1] -ne 69 -or $header[2] -ne 76 -or $header[3] -ne 70 -or $header[4] -ne 1 -or $header[5] -ne 1 -or $header[18] -ne 40 -or $header[19] -ne 0) {
        throw "Extracted library is not little-endian ARM ELF32."
    }
    Get-FileHash -LiteralPath $destination -Algorithm SHA256
    Write-Host "Prepared: $destination"
} finally { $archive.Dispose() }
