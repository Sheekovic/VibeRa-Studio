param([switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$manifestPath='patches/tracktion-ready-graph/manifest.json'
$predecessorPath='patches/tracktion-retained-midi/manifest.json'
$revision='4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86'
$directory='tracktion_engine-'+$revision
$archive='77c2d0394293e81356a2a46f03d4b90e83a5dee0e71e9e386ab2489a4abbb076'
# These reviewed raw identities deliberately agree with the checked-in manifest.
# A changed contract requires an explicit source/payload review of both files.
$approved=@(
    @{path='modules/tracktion_graph/tracktion_graph/tracktion_LockFreeMultiThreadedNodePlayer.h'; originalSha256='1c350ac885f71910ff70d39eba7f63afac911348a77f869fcfc018533d4c4ec0'; patchedSha256='74eae99b17e870566fdfaab5777fe8c4b43aae7a3ce6222ec2e8baae4bc2e03a'; patchSha256='109cd9c87aa83a4aa1748cee5356b78909cc1ec7f7466a38f5d4f6621db7d410'},
    @{path='modules/tracktion_graph/tracktion_graph/tracktion_LockFreeMultiThreadedNodePlayer.cpp'; originalSha256='ba2191de6a79281c61ac6c34dec5f81331ce6e1afcfb8c8c89cc542b90550eab'; patchedSha256='c00934716646155c4f891e0210f4276d4ae8a6c3a9177401ef8b77fb57ae2cc8'; patchSha256='236168fa5e2bc710dfc842a1f784d3fee93f516d03c43f0482d7450575881f41'}
)
function Assert-NoReparse([string]$path) {
    $candidate=[IO.Path]::GetFullPath($path)
    if ($candidate -ne $root -and
        -not $candidate.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Ready graph path escapes project root'
    }
    while ($candidate) {
        $item=Get-Item -LiteralPath $candidate -Force -ErrorAction SilentlyContinue
        if ($null -ne $item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Ready graph input crosses a reparse point'
        }
        $parent=Split-Path $candidate -Parent
        if ($parent -eq $candidate) {break}
        $candidate=$parent
    }
}
foreach ($path in @($manifestPath,$predecessorPath,'dependencies.lock.json','scripts/apply-ready-graph-patch.ps1')) {
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
    throw 'Ready graph pinned dependency contract mismatch; no source changed'
}
if ($manifest.files.Count -ne $approved.Count -or
    @($manifest.files.path | Select-Object -Unique).Count -ne $approved.Count) {
    throw 'Ready graph target set mismatch; no source changed'
}
$midiTargets=@('modules/tracktion_graph/tracktion_graph/tracktion_Node.h',
               'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h',
               'modules/tracktion_graph/tracktion_graph/tracktion_LockFreeMultiThreadedNodePlayer.cpp',
               'modules/tracktion_graph/tracktion_graph/tracktion_LockFreeMultiThreadedNodePlayer.h')
if ($predecessor.files.Count -ne 4 -or @($predecessor.files.path | Select-Object -Unique).Count -ne 4 -or
    @($predecessor.files.path | Where-Object {$_ -cnotin $midiTargets}).Count -ne 0) {
    throw 'Ready graph predecessor target set mismatch; no source changed'
}
foreach ($expected in $approved) {
    $prior=@($predecessor.files | Where-Object path -CEQ $expected.path)
    if ($prior.Count -ne 1 -or $prior[0].patchedSha256 -cne $expected.originalSha256) {
        throw 'Ready graph exact MIDI predecessor mismatch; no source changed'
    }
}
# This pinned generic player API includes no application header. It cannot certify
# external Edit/plugin/node ownership or actual adoption; root owns those gates.
if ($null -ne $manifest.sourceDependencies -or $null -ne $manifest.sourceDependency) {
    throw 'Ready graph unexpected application source dependency contract'
}
$relative='vendor/source/'+$directory
$source=[IO.Path]::GetFullPath((Join-Path $root $relative))
Assert-NoReparse $source
$changes=@()
$acceptedStableStages=@{}
$pending=@()
$scratchParent=[IO.Path]::GetFullPath((Join-Path $root 'build/ready-graph-check/preflight'))
$scratchRelative='build/ready-graph-check/preflight/'+[Guid]::NewGuid().ToString('N')
$scratch=[IO.Path]::GetFullPath((Join-Path $root $scratchRelative))
Assert-NoReparse $scratch
Push-Location $root
try {
    # Validate every live target and payload before creating any mirror or writing source.
    foreach ($entry in $manifest.files) {
        $expected=@($approved | Where-Object path -CEQ $entry.path)
        if ($expected.Count -ne 1 -or
            $entry.patch -cne ('patches/tracktion-ready-graph/'+[IO.Path]::GetFileName($entry.path)+'.patch') -or
            $entry.originalSha256 -cne $expected[0].originalSha256 -or
            $entry.patchedSha256 -cne $expected[0].patchedSha256 -or
            $entry.patchSha256 -cne $expected[0].patchSha256) {
            throw 'Ready graph exact target identity/path mismatch; no source changed'
        }
        $target=[IO.Path]::GetFullPath((Join-Path $source $entry.path))
        Assert-NoReparse $target
        if (-not $target.StartsWith($source+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Ready graph target escapes pinned source'
        }
        $patch=Join-Path $root $entry.patch
        Assert-NoReparse $patch
        if ((Get-FileHash -LiteralPath $patch).Hash.ToLowerInvariant() -cne $entry.patchSha256) {
            throw 'Ready graph patch payload identity mismatch; no source changed'
        }
        $headers=@(Get-Content -LiteralPath $patch | Where-Object {$_ -match '^(---|\+\+\+) '})
        if ($headers.Count -ne 2 -or $headers[0] -cne ('--- a/'+$entry.path) -or
            $headers[1] -cne ('+++ b/'+$entry.path)) {
            throw 'Ready graph patch header mismatch; no source changed'
        }
        $stats=@(& git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --numstat $patch)
        if ($LASTEXITCODE -ne 0 -or $stats.Count -ne 1 -or
            $stats[0] -cnotmatch ('^\d+\t\d+\t'+[regex]::Escape($entry.path)+'$')) {
            throw 'Ready graph patch changes unexpected or binary target'
        }
        $actual=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()
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
                & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-ready-graph/manifest.json' $entry.path $actual
                if ($LASTEXITCODE -ne 0) {throw 'Native phase-probe exact successor verification failed; no source changed'}
                $acceptedStableStages[$entry.path]=@{target=$target; hash=$actual}; continue
            }
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
                & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-ready-graph/manifest.json' $entry.path $actual
                if ($LASTEXITCODE -ne 0) {throw 'Stable output-stage exact successor verification failed; no source changed'}
                $acceptedStableStages[$entry.path]=@{target=$target; hash=$actual}
                continue
            }
        }
        $structuralEntry=$null
        if ($actual -cne $entry.originalSha256 -and $actual -cne $entry.patchedSha256) {
            # Only the explicit fixed-owner context successor may extend this
            # exact player postimage. Full sixteen-target raw/payload preflight.
            $contextPath='patches/tracktion-context-ready/manifest.json'
            Assert-NoReparse (Join-Path $root $contextPath)
            Assert-NoReparse (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            $context=Get-Content -LiteralPath (Join-Path $root $contextPath) -Raw | ConvertFrom-Json
            $following=@($context.files | Where-Object path -CEQ $entry.path)
            if ($following.Count -ne 1 -or $following[0].originalSha256 -cne $entry.patchedSha256 -or
                $following[0].patchedSha256 -cne $actual) {throw 'Unrecognized ready graph source preserved; no source changed'}
            & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor $manifestPath $entry.path $actual
            if ($LASTEXITCODE -ne 0) {throw 'Selected graph exact successor verification failed; no source changed'}
            $structuralEntry=$following[0]
        }
        $changes+=@{entry=$entry; target=$target; patch=$patch; initial=$actual; applied=($actual -ceq $entry.patchedSha256 -or $null -ne $structuralEntry); structuralEntry=$structuralEntry}
        if ($actual -ceq $entry.originalSha256) {$pending+=$patch}
    }
    # Reverse already-applied inputs in a disposable mirror to prove originals,
    # then apply BOTH patches there and check every exact raw post-image.
    foreach ($change in $changes) {
        $copy=Join-Path $scratch $change.entry.path
        New-Item -ItemType Directory -Path (Split-Path $copy -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $change.target -Destination $copy
        if ($null -ne $change.structuralEntry) {
            & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --reverse --directory=$scratchRelative (Join-Path $root $change.structuralEntry.patch)
            if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant() -cne $change.entry.patchedSha256) {
                throw 'Selected graph reverse did not prove exact ready-holder predecessor; no source changed'
            }
        }
        if ($change.applied) {
            & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --reverse --directory=$scratchRelative $change.patch
            if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant() -cne $change.entry.originalSha256) {
                throw 'Ready graph reverse preimage mismatch; no dependency source changed'
            }
        }
    }
    $patches=@($changes | ForEach-Object {$_.patch})
    $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                 'apply',"--directory=$scratchRelative")+$patches
    if ($patches.Count -gt 0) {
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'Ready graph isolated combined preflight failed; no dependency source changed'}
    } elseif ($pending.Count -ne 0 -or $acceptedStableStages.Count -ne $manifest.files.Count) {
        throw 'Empty ready-graph reconstruction requires every exact validated successor; no source changed'
    }
    foreach ($change in $changes) {
        if ((Get-FileHash -LiteralPath (Join-Path $scratch $change.entry.path)).Hash.ToLowerInvariant() -cne $change.entry.patchedSha256) {
            throw 'Ready graph declared raw post-image mismatch; no dependency source changed'
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
                throw 'Ready graph input changed during preflight; source preserved'
            }
        }
        $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                     'apply','--check',"--directory=$relative")+$pending
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'Ready graph final preflight failed; no dependency source changed'}
        $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                     'apply',"--directory=$relative")+$pending
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'Ready graph patch application failed'}
        foreach ($change in $changes) {
            $expected=if($null -ne $change.structuralEntry){$change.structuralEntry.patchedSha256}else{$change.entry.patchedSha256}
            if ((Get-FileHash -LiteralPath $change.target).Hash.ToLowerInvariant() -cne $expected) {
                throw 'Ready graph final raw identity mismatch'
            }
        }
    }
        foreach ($accepted in $acceptedStableStages.Values) {
            Assert-NoReparse $accepted.target
            if ((Get-FileHash -LiteralPath $accepted.target).Hash.ToLowerInvariant() -cne $accepted.hash) {
                throw 'Stable output-stage input changed during predecessor preflight; preserved'
            }
        }
    Write-Output 'Verified pinned fixed non-pooled ready-graph successor; production remains OFF; exact raw identities.'
} finally {
    Pop-Location
    if (-not $scratch.StartsWith($scratchParent+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Ready graph scratch cleanup escaped its task directory'
    }
    Assert-NoReparse $scratch
    if (Test-Path -LiteralPath $scratch) {Remove-Item -LiteralPath $scratch -Recurse -Force}
}
