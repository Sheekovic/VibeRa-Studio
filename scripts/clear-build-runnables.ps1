param()
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$build=Join-Path $root 'build/windows'
if (-not (Test-Path -LiteralPath $build)) {return}
if ((Get-Item -LiteralPath (Join-Path $root 'build')).Attributes -band [IO.FileAttributes]::ReparsePoint) {throw 'Refuse linked build root'}
function Get-OwnedOutputFiles([string]$Directory) {
    $item=Get-Item -LiteralPath $Directory
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {throw "Refuse linked output: $Directory"}
    foreach ($child in Get-ChildItem -LiteralPath $Directory -Force) {
        if ($child.Attributes -band [IO.FileAttributes]::ReparsePoint) {throw "Refuse linked output: $($child.FullName)"}
        if ($child.PSIsContainer) {Get-OwnedOutputFiles $child.FullName} else {$child}
    }
}
$names=@('VibeRaSynthEngineTests.exe','VibeRaSynthPluginTests.exe','VibeRa Studio.exe','VibeRa Plugin Worker.exe','VibeRaTests.exe','VibeRaHistoryTests.exe','VibeRaCompensationTests.exe','VibeRaPhysicalBurstTests.exe','VibeRaScannerFixture.exe','CoProducerTests.exe','AutomationTests.exe','VibeRaParameterFixture.vst3','VibeRaMixerLatencyFixture.vst3','VibeRaClockWaveformFixture.vst3','VibeRaInstrumentFixture.vst3','VibeRaDynamicInstrumentFixture.vst3','VibeRaBusChoiceFixture.vst3','VibeRaStretchExperiment.exe','VibeRaTuneExperiment.exe','VibeRaTuneCorrectionExperiment.exe','VibeRaCallbackProbe.exe')
$names+='VibeRaReleaseInstrumentFixture.vst3'
# Only canonical, generated compilation products. Never touch vendor/model tools.
$files=@(Get-OwnedOutputFiles $build | Where-Object {$_.Name -in $names})
$runtimeFolders=@()
$runtimeNames=@('model-worker.cjs','magenta-runtime.cjs','package.json','package-lock.json','fetch-magenta.mjs','anticipation-worker.py','anticipation_context.py')
foreach ($target in @('VibeRa_artefacts','VibeRaTests_artefacts')) {
    foreach ($configuration in @('Release','Debug')) {
        $path=Join-Path $build "$target/$configuration/CoProducer"
        if (-not (Test-Path -LiteralPath $path)) {continue}
        $items=@(Get-ChildItem -LiteralPath $path -Force)
        if (@($items | Where-Object {$_.PSIsContainer -or $_.Name -notin $runtimeNames -or ($_.Attributes -band [IO.FileAttributes]::ReparsePoint)}).Count) {throw 'Unexpected generated runtime content; preserve for review'}
        $runtimeFolders+=$path
        $files+=@(Get-OwnedOutputFiles $path)
    }
}
# Factory copies beside development executables are reproducible content, not
# user presets. Verify their complete identity before retiring those copies.
$factorySource=Join-Path $root 'assets/synthesizer'
foreach ($target in @('VibeRa_artefacts','VibeRaTests_artefacts','VibeRaPluginWorker_artefacts','VibeRaSynthPluginTests_artefacts')) {
    foreach ($configuration in @('Release','Debug')) {
        $path=[IO.Path]::GetFullPath((Join-Path $build "$target/$configuration/Content/Synthesizer"))
        if (-not (Test-Path -LiteralPath $path)) {continue}
        if (-not $path.StartsWith($build+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Unsafe factory cleanup path'}
        $contentFiles=@(Get-OwnedOutputFiles $path)
        $sourceFiles=@(Get-OwnedOutputFiles $factorySource)
        if ($contentFiles.Count -ne $sourceFiles.Count) {throw 'Generated factory copy has unexpected content; preserve for review'}
        foreach ($item in $contentFiles) {
            $relative=$item.FullName.Substring($path.Length+1)
            $original=Join-Path $factorySource $relative
            if (-not (Test-Path -LiteralPath $original -PathType Leaf) -or
                (Get-FileHash -LiteralPath $item.FullName).Hash -ne (Get-FileHash -LiteralPath $original).Hash) {
                throw "Changed generated factory content; preserve for review: $($item.FullName)"
            }
        }
        $runtimeFolders+=$path
        $files+=$contentFiles
    }
}
$inputs=Join-Path $root 'build/package-input'
if (Test-Path -LiteralPath $inputs) {
    $inputFiles=@(Get-OwnedOutputFiles $inputs)
    foreach ($item in $inputFiles) {
        $relative=$item.FullName.Substring($inputs.Length+1)
        if (-not ($relative -in $names -or ($relative.StartsWith('CoProducer\') -and $item.Name -in $runtimeNames))) {throw "Unexpected staging input; preserve for review: $relative"}
    }
    $files+=$inputFiles
}
$files=@($files | Sort-Object FullName -Unique)
if (-not $files.Count -and -not $runtimeFolders.Count -and -not (Test-Path -LiteralPath $inputs)) {return}
# Inspect all targets before deleting any. Never close a running instance here.
if ($files.Count) {
    $paths=@($files.FullName)
    foreach ($process in Get-CimInstance Win32_Process) {
        if ($process.ExecutablePath -and $process.ExecutablePath -in $paths) {throw "Generated output is in use by PID $($process.ProcessId); no output was removed"}
    }
}
$records=@($files | ForEach-Object {@{path=$_.FullName;bytes=[long]$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
$receipt=[ordered]@{finished=$false;deletedBytes=[long]0;files=$records;removedPaths=@();canonicalPreserved='dist/VibeRa Studio'}
$receiptPath=Join-Path $root 'build/runnable-cleanup.json'
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
foreach ($entry in $records) {
    $path=[IO.Path]::GetFullPath($entry.path)
    if (-not ($path.StartsWith($build+'\',[StringComparison]::OrdinalIgnoreCase) -or $path.StartsWith($inputs+'\',[StringComparison]::OrdinalIgnoreCase))) {throw 'Build cleanup escaped canonical output paths'}
    Remove-Item -LiteralPath $path -Force
    $receipt.deletedBytes+=$entry.bytes
    $receipt.removedPaths+=$path
}
foreach ($path in $runtimeFolders) {
    $path=[IO.Path]::GetFullPath($path)
    if (-not $path.StartsWith($build+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Unsafe runtime cleanup path'}
    Remove-Item -LiteralPath $path -Recurse -Force
}
if (Test-Path -LiteralPath $inputs) {
    if ([IO.Path]::GetFullPath($inputs) -ne (Join-Path $root 'build/package-input')) {throw 'Unsafe staging cleanup path'}
    Remove-Item -LiteralPath $inputs -Recurse -Force
}
$receipt.finished=$true
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
Write-Output "Removed $($records.Count) generated runnable/runtime files; $($receipt.deletedBytes) logical bytes. Canonical distribution preserved."
