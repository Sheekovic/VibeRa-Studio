param([ValidateSet('Release','Debug')][string]$Configuration='Release',
      [string]$Output='build/automation-owned')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$runner=Join-Path $root "build/windows/AutomationTests_artefacts/$Configuration/AutomationTests.exe"
if(-not (Test-Path -LiteralPath $runner)) {throw 'Build the automation acceptance target first.'}
$outputPath=if([IO.Path]::IsPathRooted($Output)) {[IO.Path]::GetFullPath($Output)} else {[IO.Path]::GetFullPath((Join-Path $root $Output))}
$workspace=[IO.Path]::GetFullPath($root)+[IO.Path]::DirectorySeparatorChar
if(-not $outputPath.StartsWith($workspace,[StringComparison]::OrdinalIgnoreCase)) {throw 'Automation evidence must stay inside the workspace.'}
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$log=Join-Path $outputPath 'runner.log'
$process=Start-Process -FilePath $runner -ArgumentList @(('"'+$outputPath+'"')) -PassThru -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError (Join-Path $outputPath 'runner.error.log')
$handle=$process.Handle
$receipt=[ordered]@{pid=$process.Id;creation=$process.StartTime.ToUniversalTime().ToString('o');purpose='Original standalone automation/immutable publication qualification';runnerSha256=(Get-FileHash -LiteralPath $runner).Hash;configuration=$Configuration}
$receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputPath 'process.json') -Encoding UTF8
$process.WaitForExit()
$receipt.exitCode=$process.ExitCode
$receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputPath 'process.json') -Encoding UTF8
if($process.ExitCode -ne 0) {throw "Automation qualification failed: $($process.ExitCode); $log"}
Get-Content -LiteralPath $log
Get-Content -LiteralPath (Join-Path $outputPath 'automation-verification.txt')
Get-Content -LiteralPath (Join-Path $outputPath 'automation-matrix.txt')
