param([switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$manifest=Get-Content -LiteralPath (Join-Path $root 'patches/tracktion-sidechain-bus/manifest.json') -Raw | ConvertFrom-Json
$lock=Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$dependency=@($lock.dependencies | Where-Object name -eq 'tracktion_engine')
$revision='4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86'
$directory='tracktion_engine-'+$revision
if ($dependency.Count -ne 1 -or $manifest.dependency -cne 'tracktion_engine' -or
    $manifest.revision -cne $revision -or $manifest.sourceDirectory -cne $directory -or
    $dependency[0].revision -cne $revision -or $dependency[0].sourceDirectory -cne $directory) {
    throw 'Sidechain receiver adapter pin mismatch'
}
$relative='vendor/source/'+$directory
$source=[IO.Path]::GetFullPath((Join-Path $root $relative))
$allowed=@('modules/tracktion_engine/plugins/tracktion_Plugin.h',
           'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp',
           'modules/tracktion_graph/tracktion_graph/nodes/tracktion_SummingNode.h')
if ($manifest.files.Count -ne $allowed.Count -or @($manifest.files.path | Select-Object -Unique).Count -ne $allowed.Count) {
    throw 'Unexpected sidechain patch target count'
}
function Assert-NoReparse([string]$path) {
    $candidate=[IO.Path]::GetFullPath($path)
    $ownedRoot=[IO.Path]::GetFullPath($root)
    if ($candidate -ne $ownedRoot -and -not $candidate.StartsWith($ownedRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Stable output-stage input escapes project root'
    }
    while ($candidate) {
        $item=Get-Item -LiteralPath $candidate -Force -ErrorAction SilentlyContinue
        if ($null -ne $item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Stable output-stage input crosses reparse point'
        }
        $parent=Split-Path $candidate -Parent
        if ($parent -eq $candidate) {break}
        $candidate=$parent
    }
}
$pending=@()
Push-Location $root
try {
    # Check every target before changing any source. An unknown vendor edit is
    # preserved, rather than leaving a partially applied known second file.
    foreach ($entry in $manifest.files) {
        if ($entry.path -cnotin $allowed -or $entry.patch -cne ('patches/tracktion-sidechain-bus/'+[IO.Path]::GetFileName($entry.path)+'.patch')) {
            throw 'Unexpected sidechain patch target'
        }
        if ($entry.originalSha256 -cnotmatch '^[0-9a-f]{64}$' -or $entry.patchedSha256 -cnotmatch '^[0-9a-f]{64}$') {
            throw 'Invalid sidechain patch identity'
        }
        $target=[IO.Path]::GetFullPath((Join-Path $source $entry.path))
        if (-not $target.StartsWith($source+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Sidechain patch escapes pinned source'
        }
        $patch=Join-Path $root $entry.patch
        $headers=@(Get-Content -LiteralPath $patch | Where-Object {$_ -match '^(---|\+\+\+) '})
        if ($headers.Count -ne 2 -or $headers[0] -cne ('--- a/'+$entry.path) -or $headers[1] -cne ('+++ b/'+$entry.path)) {
            throw 'Sidechain patch contains unexpected paths'
        }
        $stats=@(& git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --numstat $patch)
        if ($LASTEXITCODE -ne 0 -or $stats.Count -ne 1 -or $stats[0] -cnotmatch ('^\d+\t\d+\t'+[regex]::Escape($entry.path)+'$')) {
            throw 'Sidechain patch changes an unexpected or binary target'
        }
        $hash=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()
        if ($hash -eq $entry.patchedSha256) {continue}
        # Exact MIDI-affected native target: only full pinned successor proof
        # may accept bytes beyond this historical original/patched pair.
        if ($entry.path -ceq 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp' -and
            $hash -cne $entry.originalSha256 -and $hash -cne $entry.patchedSha256) {
            Assert-NoReparse $target
            Assert-NoReparse (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-sidechain-bus/manifest.json' $entry.path $hash
            if ($LASTEXITCODE -ne 0) {throw 'MIDI-affected native successor verification failed; no source changed'}
            continue
        }
        # Exact source-time Plugin successor: full canonical proof only,
        # preserving this historical original/patched application pair.
        if ($entry.path -ceq 'modules/tracktion_engine/plugins/tracktion_Plugin.h' -and
            $hash -cne $entry.originalSha256 -and $hash -cne $entry.patchedSha256) {
            Assert-NoReparse $target
            Assert-NoReparse (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-sidechain-bus/manifest.json' $entry.path $hash
            if ($LASTEXITCODE -ne 0) {throw 'Source-time Plugin successor verification failed; no source changed'}
            continue
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
                $following[0].patchedSha256 -ceq $hash) {
                & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-sidechain-bus/manifest.json' $entry.path $hash
                if ($LASTEXITCODE -ne 0) {throw 'Optional send block exact successor verification failed; no source changed'}
                continue
            }
        }
        # Exact stable-MIDI Summing successor: only full canonical proof,
        # never a loosely matched optional manifest or unknown source bytes.
        if ($entry.path -ceq 'modules/tracktion_graph/tracktion_graph/nodes/tracktion_SummingNode.h' -and
            $hash -cne $entry.originalSha256 -and $hash -cne $entry.patchedSha256) {
            Assert-NoReparse $target
            Assert-NoReparse (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-sidechain-bus/manifest.json' $entry.path $hash
            if ($LASTEXITCODE -ne 0) {throw 'Stable MIDI Summing successor verification failed; no source changed'}
            continue
        }
        # Exact Summing property-reuse successor only. The whole pure guard
        # validates all pinned chains and source inputs before this skip.
        $propertiesPath='patches/tracktion-summing-properties-reuse/manifest.json'
        if ($entry.path -ceq 'modules/tracktion_graph/tracktion_graph/nodes/tracktion_SummingNode.h' -and
            (Test-Path -LiteralPath (Join-Path $root $propertiesPath) -PathType Leaf)) {
            Assert-NoReparse (Join-Path $root $propertiesPath)
            Assert-NoReparse (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            $properties=Get-Content -LiteralPath (Join-Path $root $propertiesPath) -Raw | ConvertFrom-Json
            $following=@($properties.files | Where-Object path -CEQ $entry.path)
            if ($properties.files.Count -eq 1 -and $following.Count -eq 1 -and
                $following[0].patchedSha256 -ceq $hash) {
                & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-sidechain-bus/manifest.json' $entry.path $hash
                if ($LASTEXITCODE -ne 0) {throw 'Summing property-reuse successor verification failed; no source changed'}
                continue
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
            if ($stageEntry.Count -eq 1 -and $stageEntry[0].patchedSha256 -ceq $hash) {
                & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-sidechain-bus/manifest.json' $entry.path $hash
                if ($LASTEXITCODE -ne 0) {throw 'Stable output-stage exact successor verification failed; no source changed'}
                # No pending mutation for a fully proved exact successor.
                continue
            }
        }
        # Exact reviewed detached/context successor only. Full ten-target guard
        # validates every payload and explicit predecessor before accepting bytes.
        $structuralManifest='patches/tracktion-detached-graph/manifest.json'
        if ($entry.path -cin @('modules/tracktion_engine/plugins/tracktion_Plugin.h',
                                      'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp') -and
            (Test-Path -LiteralPath (Join-Path $root $structuralManifest) -PathType Leaf)) {
            $structural=Get-Content -LiteralPath (Join-Path $root $structuralManifest) -Raw | ConvertFrom-Json
            $following=@($structural.files | Where-Object path -CEQ $entry.path)
            if ($following.Count -eq 1 -and $following[0].patchedSha256 -ceq $hash) {
                & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor 'patches/tracktion-sidechain-bus/manifest.json' $entry.path $hash
                if ($LASTEXITCODE -ne 0) {throw 'Structural graph successor strict verification failed; no source changed'}
                continue
            }
        }
        # A known successor may replace this exact target, but only through the
        # explicit same-pin predecessor chain. Unknown bytes remain protected.
        $successorPath='patches/tracktion-retained-history/manifest.json'
        if ($entry.path -ceq 'modules/tracktion_graph/tracktion_graph/nodes/tracktion_SummingNode.h' -and
            (Test-Path -LiteralPath (Join-Path $root $successorPath) -PathType Leaf)) {
            $successor=Get-Content -LiteralPath (Join-Path $root $successorPath) -Raw | ConvertFrom-Json
            $following=@($successor.files | Where-Object path -CEQ $entry.path)
            if ($successor.dependency -ceq 'tracktion_engine' -and $successor.revision -ceq $revision -and
                $successor.sourceDirectory -ceq $directory -and
                $successor.appliesAfter -ceq 'patches/tracktion-sidechain-bus/manifest.json' -and
                $following.Count -eq 1 -and $following[0].originalSha256 -ceq $entry.patchedSha256 -and
                $following[0].patch -ceq ('patches/tracktion-retained-history/'+[IO.Path]::GetFileName($entry.path)+'.patch') -and
                $following[0].patchedSha256 -cmatch '^[0-9a-f]{64}$' -and
                $hash -ceq $following[0].patchedSha256) {continue}
        }
        $meteringPath='patches/tracktion-owned-metering/manifest.json'
        if ($entry.path -ceq 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp' -and
            (Test-Path -LiteralPath (Join-Path $root $meteringPath) -PathType Leaf)) {
            $metering=Get-Content -LiteralPath (Join-Path $root $meteringPath) -Raw | ConvertFrom-Json
            $following=@($metering.files | Where-Object path -CEQ $entry.path)
            if ($metering.dependency -ceq 'tracktion_engine' -and $metering.revision -ceq $revision -and
                $metering.sourceDirectory -ceq $directory -and
                $metering.appliesAfter -ceq 'patches/tracktion-sidechain-bus/manifest.json' -and
                $metering.files.Count -eq 1 -and $following.Count -eq 1 -and
                $following[0].originalSha256 -ceq $entry.patchedSha256 -and
                $following[0].patch -ceq 'patches/tracktion-owned-metering/tracktion_EditNodeBuilder.cpp.patch' -and
                $following[0].patchedSha256 -cmatch '^[0-9a-f]{64}$' -and
                $hash -ceq $following[0].patchedSha256) {continue}
        }
        if ($hash -ne $entry.originalSha256) {throw "Unrecognized vendor bytes preserved: $($entry.path)"}
        & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --check --directory=$relative $patch
        if ($LASTEXITCODE -ne 0) {throw 'Sidechain patch preflight failed; no source changed'}
        $pending+=@{entry=$entry; target=$target; patch=$patch}
    }
    if ($VerifyOnly) {
        Write-Output 'Verified pinned sidechain receiver patch inputs; no source changed.'
        return
    }
    foreach ($change in $pending) {
        & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --directory=$relative $change.patch
        if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $change.target).Hash.ToLowerInvariant() -ne $change.entry.patchedSha256) {
            throw 'Sidechain receiver patch application failed'
        }
    }
    Write-Output 'Verified pinned Tracktion sidechain receiver hook and prepared summing-property cache.'
} finally {Pop-Location}
