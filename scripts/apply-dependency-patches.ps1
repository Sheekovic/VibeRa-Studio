$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$manifest=Get-Content -LiteralPath (Join-Path $root 'patches/juce-vst3-parameters/manifest.json') -Raw | ConvertFrom-Json
$lock=Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$dependency=$lock.dependencies | Where-Object name -eq $manifest.dependency
if ($dependency.revision -ne $manifest.revision -or $dependency.sourceDirectory -ne $manifest.sourceDirectory) {throw 'Local parameter adapter patch revision mismatch'}
$sourceRelative='vendor/source/'+$manifest.sourceDirectory
$source=[IO.Path]::GetFullPath((Join-Path $root $sourceRelative))
$allowed=@('modules/juce_audio_processors_headless/utilities/juce_AudioPluginExtensions.h',
           'modules/juce_audio_processors_headless/format_types/juce_VST3PluginFormatImpl.h')
if ($manifest.files.Count -ne $allowed.Count) {throw 'Unexpected dependency patch file count'}
if (@($manifest.files.path | Select-Object -Unique).Count -ne $allowed.Count) {throw 'Duplicate dependency patch targets'}
foreach ($entry in $manifest.files) {
    if ($entry.path -cnotin $allowed -or $entry.patch -cne ('patches/juce-vst3-parameters/'+[IO.Path]::GetFileName($entry.path)+'.patch')) {throw 'Unexpected dependency patch target'}
    $target=[IO.Path]::GetFullPath((Join-Path $source $entry.path))
    if (-not $target.StartsWith($source+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {throw 'Dependency patch escapes pinned source'}
    $patch=Join-Path $root $entry.patch
    $headers=@(Get-Content -LiteralPath $patch | Where-Object {$_ -match '^(---|\+\+\+) '})
    if ($headers.Count -ne 2 -or $headers[0] -cne ('--- a/'+$entry.path) -or $headers[1] -cne ('+++ b/'+$entry.path)) {throw 'Dependency patch contains unapproved paths'}
    $hash=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()
    if ($hash -eq $entry.patchedSha256) {continue}
    if ($hash -ne $entry.originalSha256) {throw "Vendor file differs from both pinned and patched sources; preserved: $($entry.path)"}
    Push-Location $root
    # This family pins CRLF source/output bytes, while its patch text is LF.
    # Runners commonly use autocrlf=input: apply then succeeds but emits LF,
    # violating patchedSha256. Select the pinned conversion per invocation.
    try {
        & git -c "safe.directory=$root" -c core.autocrlf=true apply --verbose --directory=$sourceRelative $patch
        $patchExitCode=$LASTEXITCODE
    }
    finally { Pop-Location }
    $patchedHash=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()
    if ($patchExitCode -ne 0 -or $patchedHash -ne $entry.patchedSha256) {
        throw "Local dependency patch failed: $($entry.path); git exit=$patchExitCode; expected=$($entry.patchedSha256); actual=$patchedHash"
    }
}
Write-Output 'Verified pinned JUCE VST3 metadata/sample-event local patch.'
& (Join-Path $PSScriptRoot 'apply-mixer-send-patch.ps1')
& (Join-Path $PSScriptRoot 'apply-sidechain-bus-patch.ps1')
& (Join-Path $PSScriptRoot 'apply-retained-history-patch.ps1')
& (Join-Path $PSScriptRoot 'apply-retained-midi-patch.ps1')
& (Join-Path $PSScriptRoot 'apply-consumed-phase-patch.ps1')
& (Join-Path $PSScriptRoot 'apply-ready-graph-patch.ps1')
& (Join-Path $PSScriptRoot 'apply-owned-metering-patch.ps1')
& (Join-Path $PSScriptRoot 'apply-structural-graph-patches.ps1')
& (Join-Path $PSScriptRoot 'apply-vst3-editor-patch.ps1')
& (Join-Path $PSScriptRoot 'apply-history-preview-patch.ps1')
& (Join-Path $PSScriptRoot 'apply-prepared-history-patch.ps1')
& (Join-Path $PSScriptRoot 'apply-number-safety-patch.ps1')
& (Join-Path $PSScriptRoot 'apply-device-fence-patch.ps1')
& (Join-Path $PSScriptRoot 'apply-physical-callback-patch.ps1')
