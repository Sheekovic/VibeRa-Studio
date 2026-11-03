param([switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$manifestPath='patches/tracktion-consumed-phase/manifest.json'
$predecessorPath='patches/tracktion-retained-midi/manifest.json'
$revision='4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86'
$directory='tracktion_engine-'+$revision
$archive='77c2d0394293e81356a2a46f03d4b90e83a5dee0e71e9e386ab2489a4abbb076'
# These reviewed raw identities deliberately agree with the checked-in manifest.
# A changed contract requires explicit source/payload and closed-header review.
$approved=@(
    @{path='modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h'; originalSha256='6f5923dd8568a16947c2e62004c37c0b8d0cfc89a6f9c2f466f9fdb7e0920658'; patchedSha256='5995eed35d46e1a7356c55a97d56628ef80e772dba7d40057bb2fabd4c908845'; patchSha256='fbcd7b67c96ab9a273f5ea6e585262a662165e9d5d82265c4e077cac8782b516'}
)
$closedHeaderSha256='8d91ff781c8b316d04fe3ad240c9ec78a27d8b53d54bd17f2a2f9184654dc40b'
function Assert-NoReparse([string]$path) {
    $candidate=[IO.Path]::GetFullPath($path)
    if ($candidate -ne $root -and
        -not $candidate.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Consumed phase path escapes project root'
    }
    while ($candidate) {
        $item=Get-Item -LiteralPath $candidate -Force -ErrorAction SilentlyContinue
        if ($null -ne $item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Consumed phase input crosses a reparse point'
        }
        $parent=Split-Path $candidate -Parent
        if ($parent -eq $candidate) {break}
        $candidate=$parent
    }
}
foreach ($path in @($manifestPath,$predecessorPath,'dependencies.lock.json','scripts/apply-consumed-phase-patch.ps1','scripts/dev_support.py','src/CompensationPhasePair.h')) {
    Assert-NoReparse (Join-Path $root $path)
}
$manifest=Get-Content -LiteralPath (Join-Path $root $manifestPath) -Raw | ConvertFrom-Json
$predecessor=Get-Content -LiteralPath (Join-Path $root $predecessorPath) -Raw | ConvertFrom-Json
$lock=Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$dependency=@($lock.dependencies | Where-Object name -CEQ 'tracktion_engine')
if ($manifest.schema -ne 1 -or $dependency.Count -ne 1 -or $manifest.dependency -cne 'tracktion_engine' -or
    $manifest.revision -cne $revision -or $manifest.sourceDirectory -cne $directory -or
    $manifest.archiveSha256 -cne $archive -or $manifest.license -cne 'GPL-3.0-or-later' -or
    $manifest.appliesAfter -cne $predecessorPath -or
    $manifest.productionEnabled -isnot [bool] -or $manifest.productionEnabled -ne $false -or
    $predecessor.schema -ne 1 -or $predecessor.dependency -cne 'tracktion_engine' -or
    $predecessor.revision -cne $revision -or $predecessor.sourceDirectory -cne $directory -or
    $predecessor.archiveSha256 -cne $archive -or
    $predecessor.appliesAfter -cne 'patches/tracktion-retained-history/manifest.json' -or
    $predecessor.productionEnabled -isnot [bool] -or $predecessor.productionEnabled -ne $false -or
    $dependency[0].revision -cne $revision -or $dependency[0].sourceDirectory -cne $directory -or
    $dependency[0].sha256 -cne $archive) {
    throw 'Consumed phase pinned dependency contract mismatch; no source changed'
}
if ($manifest.files.Count -ne $approved.Count -or
    @($manifest.files.path | Select-Object -Unique).Count -ne $approved.Count) {
    throw 'Consumed phase target set mismatch; no source changed'
}
$midiTargets=@('modules/tracktion_graph/tracktion_graph/tracktion_Node.h',
               'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h',
               'modules/tracktion_graph/tracktion_graph/tracktion_LockFreeMultiThreadedNodePlayer.cpp',
               'modules/tracktion_graph/tracktion_graph/tracktion_LockFreeMultiThreadedNodePlayer.h')
$prior=@($predecessor.files | Where-Object path -CEQ $approved[0].path)
if ($predecessor.files.Count -ne 4 -or @($predecessor.files.path | Select-Object -Unique).Count -ne 4 -or
    @($predecessor.files.path | Where-Object {$_ -cnotin $midiTargets}).Count -ne 0 -or
    $prior.Count -ne 1 -or $prior[0].patchedSha256 -cne $approved[0].originalSha256) {
    throw 'Consumed phase exact MIDI predecessor mismatch; no source changed'
}
if ($manifest.sourceDependencies.Count -ne 1 -or
    $manifest.sourceDependencies[0].path -cne 'src/CompensationPhasePair.h' -or
    $manifest.sourceDependencies[0].mode -cne 'content' -or
    $manifest.sourceDependencies[0].sha256 -cne $closedHeaderSha256) {
    throw 'Consumed phase closed source contract mismatch; no source changed'
}
$header=Join-Path $root 'src/CompensationPhasePair.h'
$identity=@(& python -B -c 'import sys; sys.path.insert(0, sys.argv[1]); from dev_support import content_identity; print(content_identity(sys.argv[2])[''contentSha256''])' $PSScriptRoot $header)
if ($LASTEXITCODE -ne 0 -or $identity.Count -ne 1 -or $identity[0] -cne $closedHeaderSha256) {
    throw 'Consumed phase closed source content mismatch; no source changed'
}
$relative='vendor/source/'+$directory
$source=[IO.Path]::GetFullPath((Join-Path $root $relative))
Assert-NoReparse $source
$changes=@()
$acceptedStableStages=@{}
$pending=@()
$scratchParent=[IO.Path]::GetFullPath((Join-Path $root 'build/consumed-phase-check/preflight'))
$scratchRelative='build/consumed-phase-check/preflight/'+[Guid]::NewGuid().ToString('N')
$scratch=[IO.Path]::GetFullPath((Join-Path $root $scratchRelative))
Assert-NoReparse $scratch
Push-Location $root
try {
    # Validate every live target and payload before creating any mirror or writing source.
    foreach ($entry in $manifest.files) {
        $expected=@($approved | Where-Object path -CEQ $entry.path)
        if ($expected.Count -ne 1 -or
            $entry.patch -cne ('patches/tracktion-consumed-phase/'+[IO.Path]::GetFileName($entry.path)+'.patch') -or
            $entry.originalSha256 -cne $expected[0].originalSha256 -or
            $entry.patchedSha256 -cne $expected[0].patchedSha256 -or
            $entry.patchSha256 -cne $expected[0].patchSha256) {
            throw 'Consumed phase exact target identity/path mismatch; no source changed'
        }
        $target=[IO.Path]::GetFullPath((Join-Path $source $entry.path))
        Assert-NoReparse $target
        if (-not $target.StartsWith($source+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Consumed phase target escapes pinned source'
        }
        $patch=Join-Path $root $entry.patch
        Assert-NoReparse $patch
        if ((Get-FileHash -LiteralPath $patch).Hash.ToLowerInvariant() -cne $entry.patchSha256) {
            throw 'Consumed phase patch payload identity mismatch; no source changed'
        }
        $headers=@(Get-Content -LiteralPath $patch | Where-Object {$_ -match '^(---|\+\+\+) '})
        if ($headers.Count -ne 2 -or $headers[0] -cne ('--- a/'+$entry.path) -or
            $headers[1] -cne ('+++ b/'+$entry.path)) {
            throw 'Consumed phase patch header mismatch; no source changed'
        }
        $stats=@(& git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --numstat $patch)
        if ($LASTEXITCODE -ne 0 -or $stats.Count -ne 1 -or
            $stats[0] -cnotmatch ('^\d+\t\d+\t'+[regex]::Escape($entry.path)+'$')) {
            throw 'Consumed phase patch changes unexpected or binary target'
        }
        $actual=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()
        # Only this exact reviewed path delegates future successor admission to
        # the complete pinned authority. Original/patched bytes follow their
        # historical application paths; unknown bytes MUST fail closed here.
        if ($entry.path -ceq 'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h' -and
            $actual -cne $entry.originalSha256 -and $actual -cne $entry.patchedSha256) {
            Assert-NoReparse (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-consumed-phase/manifest.json' $entry.path $actual
            if ($LASTEXITCODE -ne 0) {throw 'LatencyNode authoritative successor verification failed; no source changed'}
            $acceptedStableStages[$entry.path]=@{target=$target; hash=$actual}; continue
        }
        # Narrow reviewed stable-stage final only; the full pure guard proves
        # every chained prefix/source/payload before this predecessor accepts it.
        $stagePath='patches/tracktion-stable-bus-stage/manifest.json'
        if (Test-Path -LiteralPath (Join-Path $root $stagePath) -PathType Leaf) {
            Assert-NoReparse (Join-Path $root $stagePath)
            Assert-NoReparse (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            $stage=Get-Content -LiteralPath (Join-Path $root $stagePath) -Raw | ConvertFrom-Json
            $stageEntry=@($stage.files | Where-Object path -CEQ $entry.path)
            if ($stageEntry.Count -eq 1 -and $stageEntry[0].patchedSha256 -ceq $actual) {
                & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-consumed-phase/manifest.json' $entry.path $actual
                if ($LASTEXITCODE -ne 0) {throw 'Stable output-stage exact successor verification failed; no source changed'}
                $acceptedStableStages[$entry.path]=@{target=$target; hash=$actual}
                continue
            }
        }
        if ($actual -cne $entry.originalSha256 -and $actual -cne $entry.patchedSha256) {
            throw 'Unrecognized consumed phase source preserved; no source changed'
        }
        $changes+=@{entry=$entry; target=$target; patch=$patch; initial=$actual; applied=($actual -ceq $entry.patchedSha256)}
        if ($actual -ceq $entry.originalSha256) {$pending+=$patch}
    }
    if ($changes.Count -eq 0) {
        foreach ($accepted in $acceptedStableStages.Values) {
            Assert-NoReparse $accepted.target
            if ((Get-FileHash -LiteralPath $accepted.target).Hash.ToLowerInvariant() -cne $accepted.hash) {
                throw 'Stable output-stage input changed during consumed-phase verification; preserved'
            }
        }
        Write-Output 'Verified exact stable output-stage successor to consumed phase; no source changed.'
        return
    }
    # Reverse already-applied inputs in a disposable mirror to prove originals,
    # then apply the phase patch there and check its exact raw post-image.
    foreach ($change in $changes) {
        $copy=Join-Path $scratch $change.entry.path
        New-Item -ItemType Directory -Path (Split-Path $copy -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $change.target -Destination $copy
        if ($change.applied) {
            & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --reverse --directory=$scratchRelative $change.patch
            if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant() -cne $change.entry.originalSha256) {
                throw 'Consumed phase reverse preimage mismatch; no dependency source changed'
            }
        }
    }
    $patches=@($changes | ForEach-Object {$_.patch})
    $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                 'apply',"--directory=$scratchRelative")+$patches
    & git @arguments
    if ($LASTEXITCODE -ne 0) {throw 'Consumed phase isolated combined preflight failed; no dependency source changed'}
    foreach ($change in $changes) {
        if ((Get-FileHash -LiteralPath (Join-Path $scratch $change.entry.path)).Hash.ToLowerInvariant() -cne $change.entry.patchedSha256) {
            throw 'Consumed phase declared raw post-image mismatch; no dependency source changed'
        }
    }
    if (-not $VerifyOnly -and $pending.Count -gt 0) {
        foreach ($accepted in $acceptedStableStages.Values) {
            Assert-NoReparse $accepted.target
            if ((Get-FileHash -LiteralPath $accepted.target).Hash.ToLowerInvariant() -cne $accepted.hash) {
                throw 'Stable output-stage input changed during predecessor preflight; preserved'
            }
        }
        foreach ($change in $changes) {
            if ((Get-FileHash -LiteralPath $change.target).Hash.ToLowerInvariant() -cne $change.initial) {
                throw 'Consumed phase input changed during preflight; source preserved'
            }
        }
        $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                     'apply','--check',"--directory=$relative")+$pending
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'Consumed phase final preflight failed; no dependency source changed'}
        $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                     'apply',"--directory=$relative")+$pending
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'Consumed phase patch application failed'}
        foreach ($change in $changes) {
            if ((Get-FileHash -LiteralPath $change.target).Hash.ToLowerInvariant() -cne $change.entry.patchedSha256) {
                throw 'Consumed phase final raw identity mismatch'
            }
        }
    }
        foreach ($accepted in $acceptedStableStages.Values) {
            Assert-NoReparse $accepted.target
            if ((Get-FileHash -LiteralPath $accepted.target).Hash.ToLowerInvariant() -cne $accepted.hash) {
                throw 'Stable output-stage input changed during predecessor preflight; preserved'
            }
        }
    Write-Output 'Verified pinned opt-in consumed-packet phase adapter; production remains OFF.'
} finally {
    Pop-Location
    if (-not $scratch.StartsWith($scratchParent+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Consumed phase scratch cleanup escaped its task directory'
    }
    Assert-NoReparse $scratch
    if (Test-Path -LiteralPath $scratch) {Remove-Item -LiteralPath $scratch -Recurse -Force}
}
