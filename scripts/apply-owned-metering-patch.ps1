param([Alias('VerifyOnly')][switch]$ValidateOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$manifestPath='patches/tracktion-owned-metering/manifest.json'
$predecessorPath='patches/tracktion-sidechain-bus/manifest.json'
$revision='4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86'
$directory='tracktion_engine-'+$revision
$allowed='modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp'
$patchPath='patches/tracktion-owned-metering/tracktion_EditNodeBuilder.cpp.patch'
function Assert-NoReparse([string]$path) {
    $candidate=[IO.Path]::GetFullPath($path)
    while ($candidate -eq $root -or
           $candidate.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        if ((Test-Path -LiteralPath $candidate) -and
            ((Get-Item -LiteralPath $candidate -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Owned metering input crosses a reparse point'
        }
        $candidate=Split-Path $candidate -Parent
    }
}
foreach ($path in @($manifestPath,$predecessorPath,'dependencies.lock.json',$patchPath)) {
    Assert-NoReparse (Join-Path $root $path)
}
$manifest=Get-Content -LiteralPath (Join-Path $root $manifestPath) -Raw | ConvertFrom-Json
$predecessor=Get-Content -LiteralPath (Join-Path $root $predecessorPath) -Raw | ConvertFrom-Json
$lock=Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$dependency=@($lock.dependencies | Where-Object name -CEQ 'tracktion_engine')
if ($dependency.Count -ne 1 -or $manifest.dependency -cne 'tracktion_engine' -or
    $manifest.revision -cne $revision -or $manifest.sourceDirectory -cne $directory -or
    $dependency[0].revision -cne $revision -or $dependency[0].sourceDirectory -cne $directory -or
    $manifest.appliesAfter -cne $predecessorPath -or $predecessor.dependency -cne 'tracktion_engine' -or
    $predecessor.revision -cne $revision -or $predecessor.sourceDirectory -cne $directory) {
    throw 'Owned metering pin or predecessor mismatch'
}
if ($manifest.files.Count -ne 1) {throw 'Unexpected owned metering target count'}
$entry=$manifest.files[0]
$prior=@($predecessor.files | Where-Object path -CEQ $allowed)
if ($entry.path -cne $allowed -or $entry.patch -cne $patchPath -or
    $entry.originalSha256 -cnotmatch '^[0-9a-f]{64}$' -or
    $entry.patchedSha256 -cnotmatch '^[0-9a-f]{64}$' -or
    $entry.patchSha256 -cnotmatch '^[0-9a-f]{64}$' -or
    $entry.originalSha256 -ceq $entry.patchedSha256 -or
    $prior.Count -ne 1 -or $prior[0].patchedSha256 -cne $entry.originalSha256) {
    throw 'Unexpected owned metering path or patch identity'
}
$relative='vendor/source/'+$directory
$target=[IO.Path]::GetFullPath((Join-Path (Join-Path $root $relative) $allowed))
Assert-NoReparse $target
$patch=Join-Path $root $patchPath
if ((Get-FileHash -LiteralPath $patch).Hash.ToLowerInvariant() -cne $entry.patchSha256) {
    throw 'Owned metering patch payload identity mismatch'
}
$headers=@(Get-Content -LiteralPath $patch | Where-Object {$_ -match '^(---|\+\+\+) '})
if ($headers.Count -ne 2 -or $headers[0] -cne ('--- a/'+$allowed) -or
    $headers[1] -cne ('+++ b/'+$allowed)) {throw 'Owned metering patch header mismatch'}
Push-Location $root
try {
    $stats=@(& git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --numstat $patch)
    if ($LASTEXITCODE -ne 0 -or $stats.Count -ne 1 -or
        $stats[0] -cnotmatch ('^\d+\t\d+\t'+[regex]::Escape($allowed)+'$')) {
        throw 'Owned metering patch changes an unexpected or binary target'
    }
    $actual=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()
    # Exact MIDI-affected native target: only full pinned successor proof
    # may accept bytes beyond this historical original/patched pair.
    if ($entry.path -ceq 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp' -and
        $actual -cne $entry.originalSha256 -and $actual -cne $entry.patchedSha256) {
        Assert-NoReparse $target
        Assert-NoReparse (Join-Path $PSScriptRoot 'guard-structural-graph.py')
        & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-owned-metering/manifest.json' $entry.path $actual
        if ($LASTEXITCODE -ne 0) {throw 'MIDI-affected native successor verification failed; no source changed'}
        Write-Output 'Verified exact canonical Builder successor after owned metering.'; return
    }
    # Exact optional send block successor only; full strict verification.
    $sendBlockPath='patches/tracktion-send-gain-block/manifest.json'
    if ($entry.path -cin @('modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp') -and
        (Test-Path -LiteralPath (Join-Path $root $sendBlockPath) -PathType Leaf)) {
        Assert-NoReparse (Join-Path $root $sendBlockPath)
        Assert-NoReparse (Join-Path $PSScriptRoot 'guard-structural-graph.py')
        $sendBlock=Get-Content -LiteralPath (Join-Path $root $sendBlockPath) -Raw | ConvertFrom-Json
        $following=@($sendBlock.files | Where-Object path -CEQ $entry.path)
        if ($sendBlock.files.Count -eq 6 -and $following.Count -eq 1 -and
            $following[0].patchedSha256 -ceq $actual) {
            & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-owned-metering/manifest.json' $entry.path $actual
            if ($LASTEXITCODE -ne 0) {throw 'Optional send block exact successor verification failed; no source changed'}
            Write-Output 'Verified exact optional send block successor after owned metering.'; return
        }
    }
        # Exact reviewed detached/context successor only. Full ten-target guard
        # validates every payload and explicit predecessor before accepting bytes.
        $structuralManifest='patches/tracktion-detached-graph/manifest.json'
        if ($entry.path -cin @('modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp') -and
            (Test-Path -LiteralPath (Join-Path $root $structuralManifest) -PathType Leaf)) {
            $structural=Get-Content -LiteralPath (Join-Path $root $structuralManifest) -Raw | ConvertFrom-Json
            $following=@($structural.files | Where-Object path -CEQ $entry.path)
            if ($following.Count -eq 1 -and $following[0].patchedSha256 -ceq $actual) {
                & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-owned-metering/manifest.json' $entry.path $actual
                if ($LASTEXITCODE -ne 0) {throw 'Structural graph successor strict verification failed; no source changed'}
                Write-Output 'Verified exact detached graph successor after owned metering; production remains OFF.'; return
            }
        }
    if ($actual -cne $entry.originalSha256 -and $actual -cne $entry.patchedSha256) {
        throw 'Unrecognized owned metering source preserved'
    }
    if ($actual -ceq $entry.originalSha256) {
        & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --check --directory=$relative $patch
        if ($LASTEXITCODE -ne 0) {throw 'Owned metering preflight failed; no source changed'}
        # Prove the declared post-image before touching the dependency source.
        # ValidateOnly may create/delete this task-owned disposable copy only.
        $scratchRelative='build/owned-metering-check/preflight/'+[Guid]::NewGuid().ToString('N')
        $scratch=[IO.Path]::GetFullPath((Join-Path $root $scratchRelative))
        Assert-NoReparse $scratch
        $copy=Join-Path $scratch $allowed
        try {
            New-Item -ItemType Directory -Path (Split-Path $copy -Parent) -Force | Out-Null
            Copy-Item -LiteralPath $target -Destination $copy
            & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --directory=$scratchRelative $patch
            if ($LASTEXITCODE -ne 0 -or
                (Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant() -cne $entry.patchedSha256) {
                throw 'Owned metering declared post-image mismatch; no dependency source changed'
            }
        } finally {
            if (-not $scratch.StartsWith((Join-Path $root 'build/owned-metering-check/preflight')+[IO.Path]::DirectorySeparatorChar,
                                         [StringComparison]::OrdinalIgnoreCase)) {
                throw 'Owned metering scratch cleanup escaped its task directory'
            }
            if (Test-Path -LiteralPath $scratch) {Remove-Item -LiteralPath $scratch -Recurse -Force}
        }
        if (-not $ValidateOnly) {
            & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --directory=$relative $patch
            if ($LASTEXITCODE -ne 0 -or
                (Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant() -cne $entry.patchedSha256) {
                throw 'Owned metering final identity mismatch'
            }
        }
    }
    Write-Output 'Verified pinned opt-in native metering omission; preview metering unchanged.'
} finally {Pop-Location}
