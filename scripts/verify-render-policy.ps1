param([Parameter(Mandatory=$true)][ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Evidence)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root ('build/'+$Evidence)
if(Test-Path -LiteralPath $output){throw 'Use a fresh evidence name; retain previous measurements'}
$runner=Join-Path $root 'build/windows/VibeRaTests_artefacts/Release/VibeRaTests.exe'
$hash=(Get-FileHash -LiteralPath $runner).Hash
New-Item -ItemType Directory -Path $output | Out-Null
$job=Start-Process -FilePath $runner -ArgumentList @('--render-check',('"'+$output+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $output 'stdout.log') -RedirectStandardError (Join-Path $output 'stderr.log')
$ownedHandle=$job.Handle
$creation=$job.StartTime.ToUniversalTime().ToString('o')
$began=[DateTime]::UtcNow
$maximumThreads=0
$timedOut=$false
try {
    while(-not $job.WaitForExit(20)){
        try {$job.Refresh();$maximumThreads=[Math]::Max($maximumThreads,$job.Threads.Count)}catch{}
        if(([DateTime]::UtcNow-$began).TotalSeconds -gt 60){
            $timedOut=$true
            $null=$job.CloseMainWindow()
            if(-not $job.WaitForExit(3000)){$job.Kill();$job.WaitForExit()}
            break
        }
    }
} finally {
    if(-not $job.HasExited){$null=$job.CloseMainWindow();if(-not $job.WaitForExit(3000)){$job.Kill();$job.WaitForExit()}}
    $record=[ordered]@{
        scope='Owned isolated render correctness/resource comparison; not live callback, listening or full-suite qualification'
        pid=$job.Id;creation=$creation;exitCode=$job.ExitCode;timedOut=$timedOut
        elapsedMs=([DateTime]::UtcNow-$began).TotalMilliseconds;maxObservedProcessThreads=$maximumThreads
        runnerSha256=$hash;unchangedBinary=$hash -eq (Get-FileHash -LiteralPath $runner).Hash
        compiledInputsSha256=(Get-FileHash -LiteralPath (Join-Path $root 'build/compiled-project-inputs.json')).Hash
    }
    $record | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'process.json') -Encoding UTF8
}
Get-Content -LiteralPath (Join-Path $output 'render-stages.txt')
if($timedOut -or $job.ExitCode -ne 0 -or -not $record.unchangedBinary){throw 'Render comparison failed; retained stage/process receipts'}
