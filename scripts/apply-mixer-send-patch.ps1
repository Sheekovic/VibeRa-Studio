$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$manifest=Get-Content -LiteralPath (Join-Path $root 'patches/tracktion-mixer-sends/manifest.json') -Raw | ConvertFrom-Json
$lock=Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$dependency=$lock.dependencies | Where-Object name -eq $manifest.dependency
if ($manifest.dependency -cne 'tracktion_engine' -or $dependency.revision -ne $manifest.revision -or $dependency.sourceDirectory -ne $manifest.sourceDirectory) {throw 'Mixer adapter pin mismatch'}
$relative='vendor/source/'+$manifest.sourceDirectory
$source=[IO.Path]::GetFullPath((Join-Path $root $relative))
$allowed=@('modules/tracktion_engine/plugins/internal/tracktion_AuxSend.h','modules/tracktion_engine/playback/graph/tracktion_AuxSendNode.cpp')
if ($manifest.files.Count -ne 2 -or @($manifest.files.path | Select-Object -Unique).Count -ne 2) {throw 'Unexpected Mixer patch file count'}
foreach ($entry in $manifest.files) {
    if ($entry.path -cnotin $allowed -or $entry.patch -cne ('patches/tracktion-mixer-sends/'+[IO.Path]::GetFileName($entry.path)+'.patch')) {throw 'Unexpected Mixer patch target'}
    $target=[IO.Path]::GetFullPath((Join-Path $source $entry.path))
    if (-not $target.StartsWith($source+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {throw 'Mixer patch escapes pinned source'}
    $patch=Join-Path $root $entry.patch
    $headers=@(Get-Content -LiteralPath $patch | Where-Object {$_ -match '^(---|\+\+\+) '})
    if ($headers.Count -ne 2 -or $headers[0] -cne ('--- a/'+$entry.path) -or $headers[1] -cne ('+++ b/'+$entry.path)) {throw 'Mixer patch contains unexpected paths'}
    $hash=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()
    if ($hash -eq $entry.patchedSha256) {continue}
    # Exact optional send block successor only; full strict verification.
    $sendBlockPath='patches/tracktion-send-gain-block/manifest.json'
    if ($entry.path -cin @('modules/tracktion_engine/plugins/internal/tracktion_AuxSend.h','modules/tracktion_engine/playback/graph/tracktion_AuxSendNode.cpp') -and
        (Test-Path -LiteralPath (Join-Path $root $sendBlockPath) -PathType Leaf)) {
        $sendBlock=Get-Content -LiteralPath (Join-Path $root $sendBlockPath) -Raw | ConvertFrom-Json
        $following=@($sendBlock.files | Where-Object path -CEQ $entry.path)
        if ($sendBlock.files.Count -eq 6 -and $following.Count -eq 1 -and
            $following[0].patchedSha256 -ceq $hash) {
            & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-mixer-sends/manifest.json' $entry.path $hash
            if ($LASTEXITCODE -ne 0) {throw 'Optional send block exact successor verification failed; no source changed'}
            continue
        }
    }
    # Exact reviewed fixed-initialisation AuxSendNode successor; no generic skip.
    $structuralManifest='patches/tracktion-detached-graph/manifest.json'
    if ($entry.path -cin @('modules/tracktion_engine/playback/graph/tracktion_AuxSendNode.cpp',
                          'modules/tracktion_engine/plugins/internal/tracktion_AuxSend.h') -and
        (Test-Path -LiteralPath (Join-Path $root $structuralManifest) -PathType Leaf)) {
        $structural=Get-Content -LiteralPath (Join-Path $root $structuralManifest) -Raw | ConvertFrom-Json
        $following=@($structural.files | Where-Object path -CEQ $entry.path)
        if ($following.Count -eq 1 -and $following[0].patchedSha256 -ceq $hash) {
            & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-mixer-sends/manifest.json' $entry.path $hash
            if ($LASTEXITCODE -ne 0) {throw 'Retained send successor strict verification failed; no source changed'}
            continue
        }
    }
    if ($hash -ne $entry.originalSha256) {throw 'Unrecognized vendor bytes preserved'}
    Push-Location $root
    try { & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --directory=$relative $patch } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant() -ne $entry.patchedSha256) {throw 'Mixer patch application failed'}
}
Write-Output 'Verified pinned Tracktion realtime send-gain hook.'
