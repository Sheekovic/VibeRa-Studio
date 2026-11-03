param([switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$manifestPath='patches/tracktion-retained-history/manifest.json'
$predecessorPath='patches/tracktion-sidechain-bus/manifest.json'
$manifest=Get-Content -LiteralPath (Join-Path $root $manifestPath) -Raw | ConvertFrom-Json
$predecessor=Get-Content -LiteralPath (Join-Path $root $predecessorPath) -Raw | ConvertFrom-Json
$lock=Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$dependency=@($lock.dependencies | Where-Object name -eq 'tracktion_engine')
$revision='4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86'
$directory='tracktion_engine-'+$revision
if ($dependency.Count -ne 1 -or $manifest.dependency -cne 'tracktion_engine' -or
    $manifest.revision -cne $revision -or $manifest.sourceDirectory -cne $directory -or
    $dependency[0].revision -cne $revision -or $dependency[0].sourceDirectory -cne $directory -or
    $manifest.appliesAfter -cne $predecessorPath -or $predecessor.dependency -cne 'tracktion_engine' -or
    $predecessor.revision -cne $revision -or $predecessor.sourceDirectory -cne $directory) {
    throw 'Retained history adapter pin or predecessor mismatch'
}
$relative='vendor/source/'+$directory
$source=[IO.Path]::GetFullPath((Join-Path $root $relative))
$allowed=@('modules/tracktion_graph/tracktion_graph/tracktion_Node.h',
           'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h',
           'modules/tracktion_graph/tracktion_graph/nodes/tracktion_SummingNode.h',
           'modules/tracktion_graph/tracktion_graph/nodes/tracktion_ConnectedNode.h',
           'modules/tracktion_graph/tracktion_graph/nodes/tracktion_CompensationHistory.h',
           'modules/tracktion_engine/playback/graph/tracktion_ClickNode.cpp',
           'modules/tracktion_engine/playback/tracktion_EditPlaybackContext.h',
           'modules/tracktion_graph/tracktion_graph/tracktion_LockFreeMultiThreadedNodePlayer.cpp',
           'modules/tracktion_graph/tracktion_graph/tracktion_LockFreeMultiThreadedNodePlayer.h',
           'modules/tracktion_engine/playback/graph/tracktion_TracktionNodePlayer.h',
           'modules/tracktion_engine/playback/tracktion_EditPlaybackContext.cpp',
           'modules/tracktion_graph/utilities/tracktion_LockFreeObject.h')
$bridge=$allowed[4]
$predecessorAllowed=@('modules/tracktion_engine/plugins/tracktion_Plugin.h',
                      'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp',
                      'modules/tracktion_graph/tracktion_graph/nodes/tracktion_SummingNode.h')
if ($predecessor.files.Count -ne $predecessorAllowed.Count -or
    @($predecessor.files.path | Select-Object -Unique).Count -ne $predecessorAllowed.Count -or
    @($predecessor.files.path | Where-Object {$_ -cnotin $predecessorAllowed}).Count -ne 0) {
    throw 'Unexpected retained history predecessor targets'
}
if ($manifest.files.Count -ne $allowed.Count -or @($manifest.files.path | Select-Object -Unique).Count -ne $allowed.Count) {
    throw 'Unexpected retained history patch target count'
}
if ($manifest.sourceDependency.path -cne 'src/CompensationHistory.h' -or
    $manifest.sourceDependency.mode -cne 'content' -or
    $manifest.sourceDependency.sha256 -cnotmatch '^[0-9a-f]{64}$') {
    throw 'Retained history source dependency contract mismatch'
}
# Original source text uses the authoritative byte-level CRLF-to-LF identity.
# Its helper preserves BOM/other bytes. Vendor and patch identities stay exact.
$sourceIdentity=@(& python -c 'import sys; sys.path.insert(0, sys.argv[1]); from dev_support import content_identity; print(content_identity(sys.argv[2])[''contentSha256''])' $PSScriptRoot (Join-Path $root $manifest.sourceDependency.path))
if ($LASTEXITCODE -ne 0 -or $sourceIdentity.Count -ne 1 -or
    $sourceIdentity[0] -cne $manifest.sourceDependency.sha256) {
    throw 'Retained history source dependency identity mismatch'
}
$sum='modules/tracktion_graph/tracktion_graph/nodes/tracktion_SummingNode.h'
$prior=@($predecessor.files | Where-Object path -CEQ $sum)
$next=@($manifest.files | Where-Object path -CEQ $sum)
if ($prior.Count -ne 1 -or $next.Count -ne 1 -or
    $prior[0].patchedSha256 -cnotmatch '^[0-9a-f]{64}$' -or
    $next[0].originalSha256 -cne $prior[0].patchedSha256) {
    throw 'Retained history SummingNode does not follow the explicit sidechain patch'
}
function Assert-NoReparseTarget([string]$path) {
    $candidate=$path
    while ($candidate -and $candidate.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        if ((Test-Path -LiteralPath $candidate) -and
            ((Get-Item -LiteralPath $candidate -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Retained history target crosses a reparse point'
        }
        $candidate=Split-Path $candidate -Parent
    }
}
$pending=@()
$acceptedMidiSuccessors=@{}
$acceptedContextSuccessors=@{}
$midiSuccessorValidated=$false
Push-Location $root
try {
    # Validate the complete bundle and every target before mutating any file.
    foreach ($entry in $manifest.files) {
        if ($entry.path -cnotin $allowed -or
            $entry.patch -cne ('patches/tracktion-retained-history/'+[IO.Path]::GetFileName($entry.path)+'.patch')) {
            throw 'Unexpected retained history patch target'
        }
        $absent=$entry.path -ceq $bridge
        if ($entry.patchedSha256 -cnotmatch '^[0-9a-f]{64}$' -or
            ($absent -and ($entry.originalAbsent -isnot [bool] -or $entry.originalAbsent -ne $true -or $null -ne $entry.originalSha256)) -or
            (-not $absent -and (($null -ne $entry.originalAbsent -and
                                ($entry.originalAbsent -isnot [bool] -or $entry.originalAbsent)) -or
                               $entry.originalSha256 -cnotmatch '^[0-9a-f]{64}$'))) {
            throw 'Invalid retained history patch identity or originalAbsent declaration'
        }
        $target=[IO.Path]::GetFullPath((Join-Path $source $entry.path))
        if (-not $target.StartsWith($source+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Retained history patch escapes pinned source'
        }
        Assert-NoReparseTarget $target
        $patch=Join-Path $root $entry.patch
        $headers=@(Get-Content -LiteralPath $patch | Where-Object {$_ -match '^(---|\+\+\+) '})
        $expectedOld=if($absent){'--- /dev/null'}else{'--- a/'+$entry.path}
        if ($headers.Count -ne 2 -or $headers[0] -cne $expectedOld -or $headers[1] -cne ('+++ b/'+$entry.path)) {
            throw 'Retained history patch contains unexpected paths'
        }
        $stats=@(& git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --numstat $patch)
        if ($LASTEXITCODE -ne 0 -or $stats.Count -ne 1 -or
            $stats[0] -cnotmatch ('^\d+\t\d+\t'+[regex]::Escape($entry.path)+'$')) {
            throw 'Retained history patch changes an unexpected or binary target'
        }
        if (Test-Path -LiteralPath $target) {
            $hash=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()
            if ($hash -ceq $entry.patchedSha256) {continue}
        # Exact MIDI-affected native target: only full pinned successor proof
        # may accept bytes beyond this historical original/patched pair.
        if ($entry.path -ceq 'modules/tracktion_graph/tracktion_graph/tracktion_Node.h' -and
            $hash -cne $entry.originalSha256 -and $hash -cne $entry.patchedSha256) {
            Assert-NoReparseTarget $target
            Assert-NoReparseTarget (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-retained-history/manifest.json' $entry.path $hash
            if ($LASTEXITCODE -ne 0) {throw 'MIDI-affected native successor verification failed; no source changed'}
            $acceptedContextSuccessors[$entry.path]=$hash; continue
        }
        # Exact owned-prefix clear successor, independently pinned full chain.
        $prefixPath='patches/tracktion-node-prefix-clear/manifest.json'
        if ($entry.path -ceq 'modules/tracktion_graph/tracktion_graph/tracktion_Node.h' -and
            (Test-Path -LiteralPath (Join-Path $root $prefixPath) -PathType Leaf)) {
            Assert-NoReparseTarget (Join-Path $root $prefixPath)
            Assert-NoReparseTarget (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            $prefixContract=Get-Content -LiteralPath (Join-Path $root $prefixPath) -Raw | ConvertFrom-Json
            $following=@($prefixContract.files | Where-Object path -CEQ $entry.path)
            if ($prefixContract.files.Count -eq 1 -and $following.Count -eq 1 -and
                $following[0].patchedSha256 -ceq $hash) {
                & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-retained-history/manifest.json' $entry.path $hash
                if ($LASTEXITCODE -ne 0) {throw 'Owned-prefix clear exact successor verification failed; no source changed'}
                $acceptedContextSuccessors[$entry.path]=$hash; continue
            }
        }
        # Exact stable-MIDI Summing successor: only full canonical proof,
        # never a loosely matched optional manifest or unknown source bytes.
        if ($entry.path -ceq 'modules/tracktion_graph/tracktion_graph/nodes/tracktion_SummingNode.h' -and
            $hash -cne $entry.originalSha256 -and $hash -cne $entry.patchedSha256) {
            Assert-NoReparseTarget $target
            Assert-NoReparseTarget (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-retained-history/manifest.json' $entry.path $hash
            if ($LASTEXITCODE -ne 0) {throw 'Stable MIDI Summing successor verification failed; no source changed'}
            $acceptedContextSuccessors[$entry.path]=$hash; continue
        }
        # Exact Summing property-reuse successor only. The whole pure guard
        # validates all pinned chains and source inputs before this skip.
        $propertiesPath='patches/tracktion-summing-properties-reuse/manifest.json'
        if ($entry.path -ceq 'modules/tracktion_graph/tracktion_graph/nodes/tracktion_SummingNode.h' -and
            (Test-Path -LiteralPath (Join-Path $root $propertiesPath) -PathType Leaf)) {
            Assert-NoReparseTarget (Join-Path $root $propertiesPath)
            Assert-NoReparseTarget (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            $properties=Get-Content -LiteralPath (Join-Path $root $propertiesPath) -Raw | ConvertFrom-Json
            $following=@($properties.files | Where-Object path -CEQ $entry.path)
            if ($properties.files.Count -eq 1 -and $following.Count -eq 1 -and
                $following[0].patchedSha256 -ceq $hash) {
                & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-retained-history/manifest.json' $entry.path $hash
                if ($LASTEXITCODE -ne 0) {throw 'Summing property-reuse successor verification failed; no source changed'}
                $acceptedContextSuccessors[$entry.path]=$hash
                continue
            }
        }
        # Only this exact reviewed path delegates future successor admission to
        # the complete pinned authority. Original/patched bytes follow their
        # historical application paths; unknown bytes MUST fail closed here.
        if ($entry.path -ceq 'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h' -and
            $hash -cne $entry.originalSha256 -and $hash -cne $entry.patchedSha256) {
            Assert-NoReparseTarget (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-retained-history/manifest.json' $entry.path $hash
            if ($LASTEXITCODE -ne 0) {throw 'LatencyNode authoritative successor verification failed; no source changed'}
            $acceptedContextSuccessors[$entry.path]=$hash; continue
        }
        # Narrow reviewed stable-stage final only; the full pure guard proves
        # every chained prefix/source/payload before this predecessor accepts it.
        $stagePath='patches/tracktion-stable-bus-stage/manifest.json'
        if (Test-Path -LiteralPath (Join-Path $root $stagePath) -PathType Leaf) {
            Assert-NoReparseTarget (Join-Path $root $stagePath)
            Assert-NoReparseTarget (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            $stage=Get-Content -LiteralPath (Join-Path $root $stagePath) -Raw | ConvertFrom-Json
            $stageEntry=@($stage.files | Where-Object path -CEQ $entry.path)
            if ($stageEntry.Count -eq 1 -and $stageEntry[0].patchedSha256 -ceq $hash) {
                & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-retained-history/manifest.json' $entry.path $hash
                if ($LASTEXITCODE -ne 0) {throw 'Stable output-stage exact successor verification failed; no source changed'}
                $acceptedContextSuccessors[$entry.path]=$hash
                continue
            }
        }
        # Exact app-bridge EPC successor: only the complete canonical proof
        # admits bytes beyond this historical original/patched pair.
        if ($entry.path -cin @('modules/tracktion_engine/playback/tracktion_EditPlaybackContext.h',
                              'modules/tracktion_engine/playback/tracktion_EditPlaybackContext.cpp') -and
            $hash -cne $entry.originalSha256 -and $hash -cne $entry.patchedSha256) {
            Assert-NoReparseTarget $target
            Assert-NoReparseTarget (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-retained-history/manifest.json' $entry.path $hash
            if ($LASTEXITCODE -ne 0) {throw 'App bridge EPC successor verification failed; no source changed'}
            $acceptedContextSuccessors[$entry.path]=$hash; continue
        }
        # Exact default-null native phase diagnostic successor only. The
        # whole strict guard proves every predecessor/payload before this skip.
        $phaseProbePath='patches/tracktion-native-phase-probe/manifest.json'
        if ($entry.path -cin @('modules/tracktion_graph/tracktion_graph/tracktion_LockFreeMultiThreadedNodePlayer.h',
                              'modules/tracktion_graph/tracktion_graph/tracktion_LockFreeMultiThreadedNodePlayer.cpp',
                              'modules/tracktion_engine/playback/graph/tracktion_TracktionNodePlayer.h',
                              'modules/tracktion_engine/playback/tracktion_EditPlaybackContext.h',
                              'modules/tracktion_engine/playback/tracktion_EditPlaybackContext.cpp') -and
            (Test-Path -LiteralPath (Join-Path $root $phaseProbePath) -PathType Leaf)) {
            Assert-NoReparseTarget (Join-Path $root $phaseProbePath)
            Assert-NoReparseTarget (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            $phaseProbe=Get-Content -LiteralPath (Join-Path $root $phaseProbePath) -Raw | ConvertFrom-Json
            $phaseProbeEntry=@($phaseProbe.files | Where-Object path -CEQ $entry.path)
            if ($phaseProbe.files.Count -eq 5 -and $phaseProbeEntry.Count -eq 1 -and
                $phaseProbeEntry[0].patchedSha256 -ceq $hash) {
                & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-retained-history/manifest.json' $entry.path $hash
                if ($LASTEXITCODE -ne 0) {throw 'Native phase-probe exact successor verification failed; no source changed'}
                $acceptedContextSuccessors[$entry.path]=$hash; continue
            }
        }
        # Exact LOCAL-CLOSED successor; the whole structural guard proves its
        # strict-source predecessor plus every reviewed path/payload before skip.
        $localSource='patches/tracktion-local-source-initial/manifest.json'
        if ($entry.path -cin @('modules/tracktion_engine/playback/tracktion_EditPlaybackContext.h',
                              'modules/tracktion_engine/playback/tracktion_EditPlaybackContext.cpp') -and
            (Test-Path -LiteralPath (Join-Path $root $localSource) -PathType Leaf)) {
            Assert-NoReparseTarget (Join-Path $root $localSource)
            $localContract=Get-Content -LiteralPath (Join-Path $root $localSource) -Raw | ConvertFrom-Json
            $localFollowing=@($localContract.files | Where-Object path -CEQ $entry.path)
            if ($localContract.appliesAfter -ceq 'patches/tracktion-source-admission/manifest.json' -and
                $localFollowing.Count -eq 1 -and $localFollowing[0].patchedSha256 -ceq $hash) {
                Assert-NoReparseTarget (Join-Path $PSScriptRoot 'guard-structural-graph.py')
                & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-retained-history/manifest.json' $entry.path $hash
                if ($LASTEXITCODE -ne 0) {throw 'LOCAL-CLOSED successor strict verification failed; no source changed'}
                $acceptedContextSuccessors[$entry.path]=$hash; continue
            }
        }
        # Exact optional source-admission successor. This full read-only guard
        # proves BOTH native predecessor chains, not just a matching file hash.
        $sourceAdmission='patches/tracktion-source-admission/manifest.json'
        if ($entry.path -cin @('modules/tracktion_engine/playback/tracktion_EditPlaybackContext.h',
                              'modules/tracktion_engine/playback/tracktion_EditPlaybackContext.cpp') -and
            (Test-Path -LiteralPath (Join-Path $root $sourceAdmission) -PathType Leaf)) {
            Assert-NoReparseTarget (Join-Path $root $sourceAdmission)
            $admission=Get-Content -LiteralPath (Join-Path $root $sourceAdmission) -Raw | ConvertFrom-Json
            $following=@($admission.files | Where-Object path -CEQ $entry.path)
            if ($following.Count -eq 1 -and $following[0].patchedSha256 -ceq $hash) {
                & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-retained-history/manifest.json' $entry.path $hash
                if ($LASTEXITCODE -ne 0) {throw 'Source admission successor strict verification failed; no source changed'}
                $acceptedContextSuccessors[$entry.path]=$hash; continue
            }
        }
        # Exact reviewed detached/context successor only. Full ten-target guard
        # validates every payload and explicit predecessor before accepting bytes.
        $structuralManifest='patches/tracktion-context-ready/manifest.json'
        if ($entry.path -cin @('modules/tracktion_engine/playback/tracktion_EditPlaybackContext.h',
                                      'modules/tracktion_engine/playback/tracktion_EditPlaybackContext.cpp',
                                      'modules/tracktion_engine/playback/graph/tracktion_TracktionNodePlayer.h',
                                      'modules/tracktion_graph/tracktion_graph/tracktion_LockFreeMultiThreadedNodePlayer.h',
                                      'modules/tracktion_graph/tracktion_graph/tracktion_LockFreeMultiThreadedNodePlayer.cpp') -and
            (Test-Path -LiteralPath (Join-Path $root $structuralManifest) -PathType Leaf)) {
            $structural=Get-Content -LiteralPath (Join-Path $root $structuralManifest) -Raw | ConvertFrom-Json
            $following=@($structural.files | Where-Object path -CEQ $entry.path)
            if ($following.Count -eq 1 -and $following[0].patchedSha256 -ceq $hash) {
                & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-retained-history/manifest.json' $entry.path $hash
                if ($LASTEXITCODE -ne 0) {throw 'Structural graph successor strict verification failed; no source changed'}
                $acceptedContextSuccessors[$entry.path]=$hash; continue
            }
        }
            # A successor is accepted only after its own complete strict guard
            # proves source content, payloads, predecessor and every post-image.
            $midiPath='patches/tracktion-retained-midi/manifest.json'
            if ($entry.path -cin @($allowed[0],$allowed[1],$allowed[7],$allowed[8]) -and
                (Test-Path -LiteralPath (Join-Path $root $midiPath) -PathType Leaf)) {
                Assert-NoReparseTarget (Join-Path $root $midiPath)
                Assert-NoReparseTarget (Join-Path $PSScriptRoot 'apply-retained-midi-patch.ps1')
                $midi=Get-Content -LiteralPath (Join-Path $root $midiPath) -Raw | ConvertFrom-Json
                $following=@($midi.files | Where-Object path -CEQ $entry.path)
                $recognizedMidiSuccessor=$false
                if ($following.Count -eq 1 -and $following[0].originalSha256 -ceq $entry.patchedSha256 -and
                    $following[0].patchedSha256 -cmatch '^[0-9a-f]{64}$') {
                    $recognizedMidiSuccessor=$hash -ceq $following[0].patchedSha256
                    $phasePath='patches/tracktion-consumed-phase/manifest.json'
                    if (-not $recognizedMidiSuccessor -and $entry.path -ceq $allowed[1] -and
                        (Test-Path -LiteralPath (Join-Path $root $phasePath) -PathType Leaf)) {
                        Assert-NoReparseTarget (Join-Path $root $phasePath)
                        $phase=Get-Content -LiteralPath (Join-Path $root $phasePath) -Raw | ConvertFrom-Json
                        $phaseEntry=@($phase.files | Where-Object path -CEQ $entry.path)
                        $recognizedMidiSuccessor=$phaseEntry.Count -eq 1 -and
                            $phaseEntry[0].originalSha256 -ceq $following[0].patchedSha256 -and
                            $phaseEntry[0].patchedSha256 -cmatch '^[0-9a-f]{64}$' -and
                            $hash -ceq $phaseEntry[0].patchedSha256
                    }
                    $readyPath='patches/tracktion-ready-graph/manifest.json'
                    if (-not $recognizedMidiSuccessor -and $entry.path -cin @($allowed[7],$allowed[8]) -and
                        (Test-Path -LiteralPath (Join-Path $root $readyPath) -PathType Leaf)) {
                        Assert-NoReparseTarget (Join-Path $root $readyPath)
                        $ready=Get-Content -LiteralPath (Join-Path $root $readyPath) -Raw | ConvertFrom-Json
                        $readyEntry=@($ready.files | Where-Object path -CEQ $entry.path)
                        $recognizedMidiSuccessor=$readyEntry.Count -eq 1 -and
                            $readyEntry[0].originalSha256 -ceq $following[0].patchedSha256 -and
                            $readyEntry[0].patchedSha256 -cmatch '^[0-9a-f]{64}$' -and
                            $hash -ceq $readyEntry[0].patchedSha256
                    }
                }
                if ($recognizedMidiSuccessor) {
                    if (-not $midiSuccessorValidated) {
                        & (Join-Path $PSScriptRoot 'apply-retained-midi-patch.ps1') -VerifyOnly
                        $midiSuccessorValidated=$true
                    }
                    $acceptedMidiSuccessors[$entry.path]=$hash
                    continue
                }
            }
            if ($absent -or $hash -cne $entry.originalSha256) {
                throw "Unrecognized retained history input preserved: $($entry.path)"
            }
        } elseif (-not $absent) {
            throw "Required retained history input absent: $($entry.path)"
        }
        $pending+=$patch
    }
    if ($pending.Count -gt 0) {
        $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                     'apply','--check',"--directory=$relative")+$pending
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'Retained history combined preflight failed; no source changed'}
    }
    if ($VerifyOnly) {
        Write-Output 'Verified pinned retained history patch inputs; no source changed.'
        return
    }
    if ($pending.Count -gt 0) {
        $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                     'apply',"--directory=$relative")+$pending
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'Retained history patch application failed'}
    }
    foreach ($entry in $manifest.files) {
        $expected=if($acceptedContextSuccessors.ContainsKey($entry.path)){$acceptedContextSuccessors[$entry.path]}elseif($acceptedMidiSuccessors.ContainsKey($entry.path)){$acceptedMidiSuccessors[$entry.path]}else{$entry.patchedSha256}
        if ((Get-FileHash -LiteralPath (Join-Path $source $entry.path)).Hash.ToLowerInvariant() -cne $expected) {
            throw 'Retained history final identity mismatch'
        }
    }
    Write-Output 'Verified pinned retained compensation history and zero-delay tap adapters.'
} finally {Pop-Location}
