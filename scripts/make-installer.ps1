# SPDX-License-Identifier: AGPL-3.0-or-later
# Builds a per-user self-extracting setup (VibeRa-Studio-Setup.exe) from a runnable
# folder with Windows' built-in IExpress: payload.zip + packaging/install.ps1 +
# packaging/uninstall.ps1. No download, no admin rights. Output goes under build/.
param(
    [string]$Source = (Join-Path $PSScriptRoot '..\dist\VibeRa Studio'),
    [string]$Output = (Join-Path $PSScriptRoot '..\build\windows\installer')
)
$ErrorActionPreference = 'Stop'
$Source = (Resolve-Path $Source).Path
if (-not (Test-Path (Join-Path $Source 'VibeRa Studio.exe'))) { throw "No VibeRa Studio.exe in $Source" }
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Output = (Resolve-Path $Output).Path
# IExpress cannot handle spaces in its paths: stage and build in a space-free temp folder.
$work = Join-Path ([IO.Path]::GetTempPath()) ('vibera-setup-' + [guid]::NewGuid().ToString('N'))
if ($work.Contains(' ')) { throw "The temp folder path has spaces: $work" }
$stage = Join-Path $work 'stage'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
Compress-Archive -Path (Join-Path $Source '*') -DestinationPath (Join-Path $stage 'payload.zip') -CompressionLevel Optimal
Copy-Item (Join-Path $PSScriptRoot '..\packaging\install.ps1') $stage
Copy-Item (Join-Path $PSScriptRoot '..\packaging\uninstall.ps1') $stage
$final = Join-Path $Output 'VibeRa-Studio-Setup.exe'
$setup = Join-Path $work 'VibeRa-Studio-Setup.exe'
$sed = Join-Path $work 'setup.sed'
@"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=0
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=
DisplayLicense=
FinishMessage=VibeRa Studio is installed. Start it from the Start Menu or the Desktop.
TargetName=$setup
FriendlyName=VibeRa Studio Setup
AppLaunched=powershell.exe -NoProfile -ExecutionPolicy Bypass -File install.ps1
PostInstallCmd=<None>
AdminQuietInstCmd=
UserQuietInstCmd=
SourceFiles=SourceFiles
[Strings]
FILE0="install.ps1"
FILE1="uninstall.ps1"
FILE2="payload.zip"
[SourceFiles]
SourceFiles0=$stage\
[SourceFiles0]
%FILE0%=
%FILE1%=
%FILE2%=
"@ | Set-Content -LiteralPath $sed -Encoding ASCII
$process = Start-Process -FilePath "$env:WINDIR\System32\iexpress.exe" -ArgumentList '/N', '/Q', $sed -Wait -PassThru -WindowStyle Hidden
if ($process.ExitCode -ne 0 -or -not (Test-Path $setup)) { throw "IExpress failed (exit $($process.ExitCode))" }
if (Test-Path $final) { Remove-Item $final -Force }
Move-Item -LiteralPath $setup -Destination $final
Remove-Item $work -Recurse -Force
Write-Output $final
