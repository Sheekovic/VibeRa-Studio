# SPDX-License-Identifier: AGPL-3.0-or-later
# Removes a per-user VibeRa Studio installation: shortcuts, the uninstall entry and the
# program folder. User projects, samples and settings elsewhere are never touched.
param(
    [string]$Target = (Split-Path -Parent $MyInvocation.MyCommand.Path),
    [switch]$NoShortcuts,
    [switch]$NoRegistry
)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $Target 'VibeRa Studio.exe'
if (-not (Test-Path $exe)) { throw "No VibeRa Studio installation at $Target" }
$running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -and ($_.Path -ieq $exe) }
if ($running) { throw 'Close VibeRa Studio before uninstalling.' }
if (-not $NoShortcuts) {
    foreach ($link in @((Join-Path ([Environment]::GetFolderPath('Programs')) 'VibeRa Studio.lnk'),
                        (Join-Path ([Environment]::GetFolderPath('Desktop')) 'VibeRa Studio.lnk'))) {
        if (Test-Path $link) { Remove-Item $link -Force }
    }
}
if (-not $NoRegistry) {
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\VibeRaStudio'
    if (Test-Path $key) { Remove-Item $key -Recurse -Force }
}
# This script lives in the folder it removes: delete from a temporary copy's process.
$cleanup = Join-Path $env:TEMP ("vibera-uninstall-" + [guid]::NewGuid().ToString('N') + '.ps1')
Set-Content -LiteralPath $cleanup -Encoding UTF8 -Value @"
Start-Sleep -Milliseconds 500
Remove-Item -LiteralPath '$Target' -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath '$cleanup' -Force -ErrorAction SilentlyContinue
"@
if ($env:VIBERA_UNINSTALL_WAIT -eq '1') {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $cleanup
} else {
    Start-Process powershell.exe -WindowStyle Hidden -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $cleanup
}
Write-Output "Uninstalled VibeRa Studio from $Target"
