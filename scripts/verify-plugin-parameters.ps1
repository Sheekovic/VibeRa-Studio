param([switch]$SkipBuild,[switch]$NativeDesktop,
      [string]$Instrument='C:/Program Files/Common Files/VST3/Arturia/Analog Lab V.vst3',
      [string]$Effect='C:/Program Files/Common Files/VST3/MeldaProduction/Modulation/MVibratoMB.vst3')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root 'build/parameter-qualification'
New-Item -ItemType Directory -Path $output -Force | Out-Null
& (Join-Path $PSScriptRoot 'apply-dependency-patches.ps1')
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release *> (Join-Path $output 'build.log')
    if ($LASTEXITCODE -ne 0) {throw 'Parameter Release build failed'}
}
$app=Join-Path $root 'dist/VibeRa Studio/VibeRa Studio.exe'
$runner=Join-Path $root 'dist/VibeRa Studio/VibeRaTests.exe'
$worker=Join-Path $root 'dist/VibeRa Studio/VibeRa Plugin Worker.exe'
$fixture=Join-Path $root 'dist/VibeRa Studio/VibeRaParameterFixture.vst3'
$proof=[ordered]@{finished=$false;started=(Get-Date).ToUniversalTime().ToString('o');buildSkipped=[bool]$SkipBuild;nativeDesktop=[bool]$NativeDesktop;processes=@();sourceHashes=@();binaries=@();
    retainedFailures=@('build/parameter-stale-display','build/parameter-provider-diagnostic-instrument.error',
                      'build/parameter-native-initial','build/parameter-native-menu-diagnostic','build/parameter-native-loaded-project',
                      'build/automation-controller-deadline-failure','build/automation-sampler-deadline-failure','build/automation-bundled-sampler-failure')}
foreach ($path in @($app,$runner,$worker,$fixture)) {
    if (-not (Test-Path -LiteralPath $path)) {throw "Missing parameter build artifact: $path"}
    $proof.binaries+=@{path=$path.Substring($root.Length+1);sha256=(Get-FileHash -LiteralPath $path).Hash}
}
Push-Location $root
try {
    $paths=@(& rg --files src tests scripts patches assets)+@('CMakeLists.txt','CMakePresets.json','dependencies.lock.json')
    if ($LASTEXITCODE -ne 0) {throw 'Cannot enumerate qualification sources'}
    foreach ($path in $paths) {$proof.sourceHashes+=@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash}}
    $manifest=Get-Content patches/juce-vst3-parameters/manifest.json -Raw | ConvertFrom-Json
    foreach ($entry in $manifest.files) {
        $path='vendor/source/'+$manifest.sourceDirectory+'/'+$entry.path
        $proof.sourceHashes+=@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash}
    }
} finally {Pop-Location}
function Save-Proof { $proof | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'qualification.json') -Encoding UTF8 }
function Invoke-Owned([string]$Executable,[string[]]$Arguments,[string]$Purpose,[int]$TimeoutSeconds=180) {
    foreach ($argument in $Arguments) {if ($argument -match '["\r\n]') {throw 'Unexpected process argument quoting'}}
    $log=Join-Path $output ($Purpose+'.log')
    $process=Start-Process -FilePath $Executable -ArgumentList @($Arguments | ForEach-Object {'"'+$_+'"'}) -PassThru -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError ($log+'.error')
    $retainedHandle=$process.Handle
    $receipt=[ordered]@{purpose=$Purpose;pid=$process.Id;creation=$process.StartTime.ToUniversalTime().ToString('o');exitCode=$null;timedOut=$false}
    $proof.processes+=$receipt; Save-Proof
    if (-not $process.WaitForExit($TimeoutSeconds*1000)) {
        $receipt.timedOut=$true
        $null=$process.CloseMainWindow()
        if (-not $process.WaitForExit(2000)) {$process.Kill();$process.WaitForExit()}
    }
    $receipt.exitCode=$process.ExitCode; Save-Proof
    if ($receipt.timedOut -or $receipt.exitCode -ne 0) {throw "$Purpose failed; retained logs: $log"}
}
try {
    Save-Proof
    $vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $ctest=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
    $sourceTestsStarted=(Get-Date).ToUniversalTime()
    Invoke-Owned $ctest @('--test-dir',(Join-Path $root 'build/windows'),'-C','Release','--output-on-failure','--no-tests=error') 'source-ctest' 360
    foreach ($provider in @(@{name='instrument';path=$Instrument},@{name='effect';path=$Effect})) {
        Invoke-Owned $runner @('--parameter-provider-check',(Join-Path $output $provider.name),$provider.path) ('native-'+$provider.name)
    }
    if ($NativeDesktop) {
        # Never pick an old flat project or the newest directory by name alone.
        # Require this invocation's successful GUI receipt and the exact app and
        # original fixture identities before consuming its disposable project.
        $expectedApp=(Get-FileHash -LiteralPath $app).Hash.ToLowerInvariant()
        $expectedFixture=(Get-FileHash -LiteralPath $fixture).Hash.ToLowerInvariant()
        $expectedWorker=(Get-FileHash -LiteralPath $worker).Hash.ToLowerInvariant()
        $parameterProject=$null
        $attemptRoot=Join-Path $root 'build/windows/parameter_gui_acceptance'
        foreach($attempt in (Get-ChildItem -LiteralPath $attemptRoot -Directory -Filter 'attempt-*')) {
            if($attempt.Attributes -band [IO.FileAttributes]::ReparsePoint){continue}
            $receiptPath=Join-Path $attempt.FullName 'verification.json'
            if(!(Test-Path -LiteralPath $receiptPath -PathType Leaf) -or
               (Get-Item -LiteralPath $receiptPath).LastWriteTimeUtc -lt $sourceTestsStarted){continue}
            $guiReceipt=Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
            if(!$guiReceipt.finished -or !$guiReceipt.passed -or
               $guiReceipt.executableSha256 -ne $expectedApp -or $guiReceipt.fixtureSha256 -ne $expectedFixture -or
               $guiReceipt.workerSha256 -ne $expectedWorker){continue}
            $candidateProject=Join-Path $attempt.FullName 'parameter-song.vibra'
            if($guiReceipt.project -ne $candidateProject -or !(Test-Path -LiteralPath $candidateProject -PathType Leaf)){continue}
            if((Get-Item -LiteralPath $candidateProject).Attributes -band [IO.FileAttributes]::ReparsePoint){continue}
            if((Get-FileHash -LiteralPath $candidateProject).Hash.ToLowerInvariant() -ne $guiReceipt.projectSha256){continue}
            if($parameterProject){throw 'Ambiguous matching parameter GUI attempts; do not guess'}
            $parameterProject=$candidateProject
        }
        if(!$parameterProject){throw 'No fresh passed parameter GUI project matches the exact selected app/fixture'}
        $python=(Get-Command python).Source
        Invoke-Owned $python @((Join-Path $root 'scripts/verify-parameter-desktop.py'),$app,(Join-Path $output 'native-desktop'),
                              $parameterProject) 'native-desktop'
    }
    foreach ($entry in $proof.sourceHashes+$proof.binaries) {
        if ((Get-FileHash -LiteralPath (Join-Path $root $entry.path)).Hash -ne $entry.sha256) {throw "Qualification input changed during execution: $($entry.path)"}
    }
    $proof.finished=$true; $proof.finishedAt=(Get-Date).ToUniversalTime().ToString('o'); Save-Proof
} catch {
    $proof.error=$_.Exception.Message; Save-Proof; throw
}
