param([switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$manifestPath='patches/tracktion-retained-midi/manifest.json'
$predecessorPath='patches/tracktion-retained-history/manifest.json'
$revision='4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86'
$directory='tracktion_engine-'+$revision
$archive='77c2d0394293e81356a2a46f03d4b90e83a5dee0e71e9e386ab2489a4abbb076'
$allowed=@('modules/tracktion_graph/tracktion_graph/tracktion_Node.h',
           'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h',
           'modules/tracktion_graph/tracktion_graph/tracktion_LockFreeMultiThreadedNodePlayer.cpp',
           'modules/tracktion_graph/tracktion_graph/tracktion_LockFreeMultiThreadedNodePlayer.h')
function Assert-NoReparse([string]$path) {
    $candidate=[IO.Path]::GetFullPath($path)
    if ($candidate -ne $root -and
        -not $candidate.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Bounded MIDI path escapes project root'
    }
    while ($candidate) {
        if ((Test-Path -LiteralPath $candidate) -and
            ((Get-Item -LiteralPath $candidate -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Bounded MIDI input crosses a reparse point'
        }
        $parent=Split-Path $candidate -Parent
        if ($parent -eq $candidate) {break}
        $candidate=$parent
    }
}
foreach ($path in @($manifestPath,$predecessorPath,'dependencies.lock.json','scripts/dev_support.py')) {
    Assert-NoReparse (Join-Path $root $path)
}
$manifest=Get-Content -LiteralPath (Join-Path $root $manifestPath) -Raw | ConvertFrom-Json
$predecessor=Get-Content -LiteralPath (Join-Path $root $predecessorPath) -Raw | ConvertFrom-Json
$lock=Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$dependency=@($lock.dependencies | Where-Object name -CEQ 'tracktion_engine')
if ($manifest.schema -ne 1 -or $dependency.Count -ne 1 -or $manifest.dependency -cne 'tracktion_engine' -or
    $manifest.revision -cne $revision -or $manifest.sourceDirectory -cne $directory -or
    $manifest.archiveSha256 -cne $archive -or $manifest.appliesAfter -cne $predecessorPath -or
    $manifest.productionEnabled -isnot [bool] -or $manifest.productionEnabled -ne $false -or
    $dependency[0].revision -cne $revision -or $dependency[0].sourceDirectory -cne $directory -or
    $dependency[0].sha256 -cne $archive -or $predecessor.dependency -cne 'tracktion_engine' -or
    $predecessor.revision -cne $revision -or $predecessor.sourceDirectory -cne $directory) {
    throw 'Bounded MIDI pin, predecessor or production-OFF contract mismatch'
}
if ($manifest.files.Count -ne $allowed.Count -or @($manifest.files.path | Select-Object -Unique).Count -ne $allowed.Count -or
    @($manifest.files.path | Where-Object {$_ -cnotin $allowed}).Count -ne 0) {
    throw 'Unexpected bounded MIDI patch target set'
}
if ($manifest.sourceDependencies.Count -ne 1 -or
    $manifest.sourceDependencies[0].path -cne 'src/MidiCompensationHistory.h' -or
    $manifest.sourceDependencies[0].mode -cne 'content' -or
    $manifest.sourceDependencies[0].sha256 -cnotmatch '^[0-9a-f]{64}$' -or
    $predecessor.sourceDependency.path -cne 'src/CompensationHistory.h' -or
    $predecessor.sourceDependency.mode -cne 'content' -or
    $predecessor.sourceDependency.sha256 -cnotmatch '^[0-9a-f]{64}$') {
    throw 'Bounded MIDI source content contract mismatch'
}
$sourceHeader=Join-Path $root $manifest.sourceDependencies[0].path
Assert-NoReparse $sourceHeader
$priorHeader=Join-Path $root $predecessor.sourceDependency.path
Assert-NoReparse $priorHeader
$sourceIdentity=@(& python -B -c 'import sys; sys.path.insert(0, sys.argv[1]); from dev_support import content_identity; [print(content_identity(p)[''contentSha256'']) for p in sys.argv[2:]]' $PSScriptRoot $sourceHeader $priorHeader)
if ($LASTEXITCODE -ne 0 -or $sourceIdentity.Count -ne 2 -or
    $sourceIdentity[0] -cne $manifest.sourceDependencies[0].sha256 -or
    $sourceIdentity[1] -cne $predecessor.sourceDependency.sha256) {
    throw 'Bounded MIDI source dependency identity mismatch'
}
$relative='vendor/source/'+$directory
$source=[IO.Path]::GetFullPath((Join-Path $root $relative))
Assert-NoReparse $source
$changes=@()
$acceptedStableStages=@{}
$pending=@()
$phasePath='patches/tracktion-consumed-phase/manifest.json'
$phaseTarget='modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h'
$phaseValidated=$false
$phaseManifest=$null
$phaseEntry=$null
$readyPath='patches/tracktion-ready-graph/manifest.json'
$readyTargets=@($allowed[2],$allowed[3])
$readyValidated=$false
$readyManifest=$null
$scratchParent=[IO.Path]::GetFullPath((Join-Path $root 'build/retained-midi-check/preflight'))
$scratchRelative='build/retained-midi-check/preflight/'+[Guid]::NewGuid().ToString('N')
$scratch=[IO.Path]::GetFullPath((Join-Path $root $scratchRelative))
Assert-NoReparse $scratch
Push-Location $root
try {
    # Validate every path/payload/target before touching any dependency source.
    foreach ($entry in $manifest.files) {
        $prior=@($predecessor.files | Where-Object path -CEQ $entry.path)
        if ($entry.patch -cne ('patches/tracktion-retained-midi/'+[IO.Path]::GetFileName($entry.path)+'.patch') -or
            $entry.originalSha256 -cnotmatch '^[0-9a-f]{64}$' -or
            $entry.patchedSha256 -cnotmatch '^[0-9a-f]{64}$' -or
            $entry.patchSha256 -cnotmatch '^[0-9a-f]{64}$' -or
            $entry.originalSha256 -ceq $entry.patchedSha256 -or $prior.Count -ne 1 -or
            $prior[0].patchedSha256 -cne $entry.originalSha256) {
            throw 'Bounded MIDI target identity or explicit predecessor mismatch'
        }
        $target=[IO.Path]::GetFullPath((Join-Path $source $entry.path))
        Assert-NoReparse $target
        $patch=Join-Path $root $entry.patch
        Assert-NoReparse $patch
        if ((Get-FileHash -LiteralPath $patch).Hash.ToLowerInvariant() -cne $entry.patchSha256) {
            throw 'Bounded MIDI patch payload identity mismatch'
        }
        $headers=@(Get-Content -LiteralPath $patch | Where-Object {$_ -match '^(---|\+\+\+) '})
        if ($headers.Count -ne 2 -or $headers[0] -cne ('--- a/'+$entry.path) -or
            $headers[1] -cne ('+++ b/'+$entry.path)) {
            throw 'Bounded MIDI patch header mismatch'
        }
        $stats=@(& git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --numstat $patch)
        if ($LASTEXITCODE -ne 0 -or $stats.Count -ne 1 -or
            $stats[0] -cnotmatch ('^\d+\t\d+\t'+[regex]::Escape($entry.path)+'$')) {
            throw 'Bounded MIDI patch changes unexpected or binary target'
        }
        $actual=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()
        # Exact MIDI-affected native target: only full pinned successor proof
        # may accept bytes beyond this historical original/patched pair.
        if ($entry.path -ceq 'modules/tracktion_graph/tracktion_graph/tracktion_Node.h' -and
            $actual -cne $entry.originalSha256 -and $actual -cne $entry.patchedSha256) {
            Assert-NoReparse $target
            Assert-NoReparse (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-retained-midi/manifest.json' $entry.path $actual
            if ($LASTEXITCODE -ne 0) {throw 'MIDI-affected native successor verification failed; no source changed'}
            $acceptedStableStages[$entry.path]=@{target=$target; hash=$actual}; continue
        }
        # Exact owned-prefix clear successor, independently pinned full chain.
        $prefixPath='patches/tracktion-node-prefix-clear/manifest.json'
        if ($entry.path -ceq 'modules/tracktion_graph/tracktion_graph/tracktion_Node.h' -and
            (Test-Path -LiteralPath (Join-Path $root $prefixPath) -PathType Leaf)) {
            Assert-NoReparse (Join-Path $root $prefixPath)
            Assert-NoReparse (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            $prefixContract=Get-Content -LiteralPath (Join-Path $root $prefixPath) -Raw | ConvertFrom-Json
            $following=@($prefixContract.files | Where-Object path -CEQ $entry.path)
            if ($prefixContract.files.Count -eq 1 -and $following.Count -eq 1 -and
                $following[0].patchedSha256 -ceq $actual) {
                & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-retained-midi/manifest.json' $entry.path $actual
                if ($LASTEXITCODE -ne 0) {throw 'Owned-prefix clear exact successor verification failed; no source changed'}
                $acceptedStableStages[$entry.path]=@{target=$target; hash=$actual}; continue
            }
        }
        # Only this exact reviewed path delegates future successor admission to
        # the complete pinned authority. Original/patched bytes follow their
        # historical application paths; unknown bytes MUST fail closed here.
        if ($entry.path -ceq 'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h' -and
            $actual -cne $entry.originalSha256 -and $actual -cne $entry.patchedSha256) {
            Assert-NoReparse (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-retained-midi/manifest.json' $entry.path $actual
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
                & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-retained-midi/manifest.json' $entry.path $actual
                if ($LASTEXITCODE -ne 0) {throw 'Stable output-stage exact successor verification failed; no source changed'}
                $acceptedStableStages[$entry.path]=@{target=$target; hash=$actual}
                continue
            }
        }
        # Exact default-null native phase diagnostic successor only. The
        # whole strict guard proves every predecessor/payload before this skip.
        $phaseProbePath='patches/tracktion-native-phase-probe/manifest.json'
        if ($entry.path -cin @('modules/tracktion_graph/tracktion_graph/tracktion_LockFreeMultiThreadedNodePlayer.h',
                              'modules/tracktion_graph/tracktion_graph/tracktion_LockFreeMultiThreadedNodePlayer.cpp') -and
            (Test-Path -LiteralPath (Join-Path $root $phaseProbePath) -PathType Leaf)) {
            Assert-NoReparse (Join-Path $root $phaseProbePath)
            Assert-NoReparse (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            $phaseProbe=Get-Content -LiteralPath (Join-Path $root $phaseProbePath) -Raw | ConvertFrom-Json
            $phaseProbeEntry=@($phaseProbe.files | Where-Object path -CEQ $entry.path)
            if ($phaseProbe.files.Count -eq 5 -and $phaseProbeEntry.Count -eq 1 -and
                $phaseProbeEntry[0].patchedSha256 -ceq $actual) {
                & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-retained-midi/manifest.json' $entry.path $actual
                if ($LASTEXITCODE -ne 0) {throw 'Native phase-probe exact successor verification failed; no source changed'}
                $acceptedStableStages[$entry.path]=@{target=$target; hash=$actual}; continue
            }
        }
        $phaseApplied=$false
        $readyApplied=$false
        $readyEntry=$null
        $structuralEntry=$null
        if ($actual -cne $entry.originalSha256 -and $actual -cne $entry.patchedSha256) {
            # Only the one explicit successor may extend this predecessor.
            # Validate its complete guard before accepting any successor bytes.
            if ($entry.path -ceq $phaseTarget -and
                (Test-Path -LiteralPath (Join-Path $root $phasePath) -PathType Leaf)) {
                Assert-NoReparse (Join-Path $root $phasePath)
                Assert-NoReparse (Join-Path $PSScriptRoot 'apply-consumed-phase-patch.ps1')
                $phaseManifest=Get-Content -LiteralPath (Join-Path $root $phasePath) -Raw | ConvertFrom-Json
                $following=@($phaseManifest.files | Where-Object path -CEQ $entry.path)
                if ($following.Count -eq 1 -and $following[0].originalSha256 -ceq $entry.patchedSha256 -and
                    $following[0].patchedSha256 -cmatch '^[0-9a-f]{64}$' -and
                    $actual -ceq $following[0].patchedSha256) {
                    & (Join-Path $PSScriptRoot 'apply-consumed-phase-patch.ps1') -VerifyOnly
                    $phaseValidated=$true
                    $phaseEntry=$following[0]
                    $phaseApplied=$true
                }
            }
            if (-not $phaseApplied -and $entry.path -cin $readyTargets -and
                (Test-Path -LiteralPath (Join-Path $root $readyPath) -PathType Leaf)) {
                Assert-NoReparse (Join-Path $root $readyPath)
                Assert-NoReparse (Join-Path $PSScriptRoot 'apply-ready-graph-patch.ps1')
                $readyManifest=Get-Content -LiteralPath (Join-Path $root $readyPath) -Raw | ConvertFrom-Json
                $following=@($readyManifest.files | Where-Object path -CEQ $entry.path)
                $contextPath='patches/tracktion-context-ready/manifest.json'
                if ($following.Count -eq 1 -and $actual -cne $following[0].patchedSha256 -and
                    (Test-Path -LiteralPath (Join-Path $root $contextPath) -PathType Leaf)) {
                    Assert-NoReparse (Join-Path $root $contextPath)
                    Assert-NoReparse (Join-Path $PSScriptRoot 'guard-structural-graph.py')
                    $context=Get-Content -LiteralPath (Join-Path $root $contextPath) -Raw | ConvertFrom-Json
                    $afterReady=@($context.files | Where-Object path -CEQ $entry.path)
                    if ($afterReady.Count -eq 1 -and $afterReady[0].originalSha256 -ceq $following[0].patchedSha256 -and
                        $afterReady[0].patchedSha256 -ceq $actual) {
                        & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor $manifestPath $entry.path $actual
                        if ($LASTEXITCODE -ne 0) {throw 'Selected graph exact MIDI successor validation failed; no source changed'}
                        $structuralEntry=$afterReady[0]
                    }
                }
                if ($following.Count -eq 1 -and $following[0].originalSha256 -ceq $entry.patchedSha256 -and
                    $following[0].patchedSha256 -cmatch '^[0-9a-f]{64}$' -and
                    ($actual -ceq $following[0].patchedSha256 -or $null -ne $structuralEntry)) {
                    if (-not $readyValidated) {
                        & (Join-Path $PSScriptRoot 'apply-ready-graph-patch.ps1') -VerifyOnly
                        $readyValidated=$true
                    }
                    $readyApplied=$true
                    $readyEntry=$following[0]
                }
            }
            if (-not $phaseApplied -and -not $readyApplied) {
                throw 'Unrecognized bounded MIDI source preserved; no source changed'
            }
        }
        $changes+=@{entry=$entry; target=$target; patch=$patch;
                    applied=($actual -ceq $entry.patchedSha256 -or $phaseApplied -or $readyApplied);
                    phaseApplied=$phaseApplied; readyApplied=$readyApplied; readyEntry=$readyEntry; structuralEntry=$structuralEntry}
        if ($actual -ceq $entry.originalSha256) {$pending+=$patch}
    }
    # Reconstruct originals for already-applied inputs, then prove all complete
    # post-images in an owned disposable mirror. VerifyOnly also checks payload
    # consistency when every live target is already patched.
    foreach ($change in $changes) {
        $copy=Join-Path $scratch $change.entry.path
        New-Item -ItemType Directory -Path (Split-Path $copy -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $change.target -Destination $copy
        if ($change.phaseApplied) {
            if (-not $phaseValidated -or $change.entry.path -cne $phaseTarget -or $null -eq $phaseEntry) {
                throw 'Consumed phase successor was not validated before MIDI reconstruction'
            }
            & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --reverse --directory=$scratchRelative (Join-Path $root $phaseEntry.patch)
            if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant() -cne $change.entry.patchedSha256) {
                throw 'Consumed phase reverse did not prove the exact MIDI post-image; no dependency source changed'
            }
        }
        if ($null -ne $change.structuralEntry) {
            & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --reverse --directory=$scratchRelative (Join-Path $root $change.structuralEntry.patch)
            if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant() -cne $change.readyEntry.patchedSha256) {
                throw 'Selected graph reverse did not prove ready-holder bytes before MIDI reverse; no source changed'
            }
        }
        if ($change.readyApplied) {
            if (-not $readyValidated -or $change.entry.path -cnotin $readyTargets -or $null -eq $change.readyEntry) {
                throw 'Ready graph successor not validated before MIDI reconstruction'
            }
            & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --reverse --directory=$scratchRelative (Join-Path $root $change.readyEntry.patch)
            if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant() -cne $change.entry.patchedSha256) {
                throw 'Ready graph reverse did not prove exact MIDI post-image; no dependency source changed'
            }
        }
        if ($change.applied) {
            & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --reverse --directory=$scratchRelative $change.patch
            if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant() -cne $change.entry.originalSha256) {
                throw 'Bounded MIDI reverse preimage mismatch; no dependency source changed'
            }
        }
    }
    $patches=@($changes | ForEach-Object {$_.patch})
    $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                 'apply',"--directory=$scratchRelative")+$patches
    if ($patches.Count -gt 0) {
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'Bounded MIDI isolated combined preflight failed; no dependency source changed'}
    } elseif ($pending.Count -ne 0 -or $acceptedStableStages.Count -ne $manifest.files.Count) {
        throw 'Empty MIDI reconstruction requires every exact validated successor; no source changed'
    }
    foreach ($change in $changes) {
        if ((Get-FileHash -LiteralPath (Join-Path $scratch $change.entry.path)).Hash.ToLowerInvariant() -cne $change.entry.patchedSha256) {
            throw 'Bounded MIDI declared post-image mismatch; no dependency source changed'
        }
    }
    if (-not $VerifyOnly -and $pending.Count -gt 0) {
        foreach ($accepted in $acceptedStableStages.Values) {
            Assert-NoReparse $accepted.target
            if ((Get-FileHash -LiteralPath $accepted.target).Hash.ToLowerInvariant() -cne $accepted.hash) {
                throw 'Stable output-stage input changed during predecessor preflight; preserved'
            }
        }
        $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                     'apply','--check',"--directory=$relative")+$pending
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'Bounded MIDI final preflight failed; no dependency source changed'}
        $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                     'apply',"--directory=$relative")+$pending
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'Bounded MIDI patch application failed'}
        foreach ($change in $changes) {
            $expected=if($null -ne $change.structuralEntry){$change.structuralEntry.patchedSha256}elseif($change.phaseApplied){$phaseEntry.patchedSha256}elseif($change.readyApplied){$change.readyEntry.patchedSha256}else{$change.entry.patchedSha256}
            if ((Get-FileHash -LiteralPath $change.target).Hash.ToLowerInvariant() -cne $expected) {
                throw 'Bounded MIDI final identity mismatch'
            }
        }
    }
        foreach ($accepted in $acceptedStableStages.Values) {
            Assert-NoReparse $accepted.target
            if ((Get-FileHash -LiteralPath $accepted.target).Hash.ToLowerInvariant() -cne $accepted.hash) {
                throw 'Stable output-stage input changed during predecessor preflight; preserved'
            }
        }
    Write-Output 'Verified pinned opt-in bounded MIDI successor; production remains OFF.'
} finally {
    Pop-Location
    if (-not $scratch.StartsWith($scratchParent+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Bounded MIDI scratch cleanup escaped its task directory'
    }
    Assert-NoReparse $scratch
    if (Test-Path -LiteralPath $scratch) {Remove-Item -LiteralPath $scratch -Recurse -Force}
}
