# SPDX-License-Identifier: AGPL-3.0-or-later
# VibeRa Studio per-user installer (run by the self-extracting setup; no admin rights).
# Unpacks payload.zip into %LOCALAPPDATA%\Programs\VibeRa Studio, adds Start Menu and
# Desktop shortcuts, and registers an uninstaller for the current user only.
param(
    [string]$Target = (Join-Path $env:LOCALAPPDATA 'Programs\VibeRa Studio'),
    [switch]$NoShortcuts,
    [switch]$NoRegistry
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$payload = Join-Path $here 'payload.zip'
if (-not (Test-Path $payload)) { throw "payload.zip is missing next to the installer script" }

# Never overwrite an instance that is running from the target folder.
$exe = Join-Path $Target 'VibeRa Studio.exe'
$running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -and ($_.Path -ieq $exe) }
if ($running) { throw "VibeRa Studio is running from $Target. Close it and run setup again." }

$staging = "$Target.installing"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
Expand-Archive -LiteralPath $payload -DestinationPath $staging -Force
if (-not (Test-Path (Join-Path $staging 'VibeRa Studio.exe'))) { throw 'The payload has no VibeRa Studio.exe' }
if (Test-Path $Target) { Remove-Item $Target -Recurse -Force }
Move-Item -LiteralPath $staging -Destination $Target
Copy-Item (Join-Path $here 'uninstall.ps1') (Join-Path $Target 'uninstall.ps1') -Force

# The app's own version resource is authoritative; provenance carries none in release builds.
$version = (Get-Item -LiteralPath (Join-Path $Target 'VibeRa Studio.exe')).VersionInfo.ProductVersion
$provenance = Join-Path $Target 'provenance.json'
if (-not $version -and (Test-Path $provenance)) {
    try { $version = (Get-Content $provenance -Raw -Encoding UTF8 | ConvertFrom-Json).version } catch { }
}
if (-not $version) { $version = 'unknown' }

if (-not $NoShortcuts) {
    $shell = New-Object -ComObject WScript.Shell
    $places = @((Join-Path ([Environment]::GetFolderPath('Programs')) 'VibeRa Studio.lnk'),
                (Join-Path ([Environment]::GetFolderPath('Desktop')) 'VibeRa Studio.lnk'))
    foreach ($link in $places) {
        $shortcut = $shell.CreateShortcut($link)
        $shortcut.TargetPath = $exe
        $shortcut.WorkingDirectory = $Target
        $shortcut.IconLocation = "$exe,0"
        $shortcut.Save()
    }
}
if (-not $NoRegistry) {
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\VibeRaStudio'
    New-Item -Path $key -Force | Out-Null
    $uninstall = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$Target\uninstall.ps1`""
    Set-ItemProperty $key DisplayName 'VibeRa Studio'
    Set-ItemProperty $key DisplayVersion $version
    Set-ItemProperty $key Publisher 'VibeRa Studio contributors'
    Set-ItemProperty $key InstallLocation $Target
    Set-ItemProperty $key DisplayIcon $exe
    Set-ItemProperty $key UninstallString $uninstall
    Set-ItemProperty $key NoModify 1 -Type DWord
    Set-ItemProperty $key NoRepair 1 -Type DWord
}
Write-Output "Installed VibeRa Studio $version to $Target"
