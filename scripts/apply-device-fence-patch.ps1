param([switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$manifestPath='patches/tracktion-device-fence/manifest.json'
$revision='4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86'
$directory='tracktion_engine-'+$revision
$archive='77c2d0394293e81356a2a46f03d4b90e83a5dee0e71e9e386ab2489a4abbb076'
# These reviewed raw identities deliberately agree with the checked-in manifest.
# A changed contract requires an explicit source/payload review of the one target and public header.
$approved=@(
    @{path='modules/tracktion_engine/playback/tracktion_DeviceManager.h'; originalSha256='7ab4590fa518e44b977617dc003597adf84ade0021e6ce01bcf4ebcb6937af50'; patchedSha256='9523c9c6573414912477776adf0ca941d0e807938362391e8ba3de2efc621476'; patchSha256='d657269c6e8da0085965161f8c3f0d7a58f82cd5ae3dcbaaf0390112a2027a1d'},
    @{path='modules/tracktion_engine/playback/tracktion_DeviceManager.cpp'; originalSha256='28f900c36a68f768706bad72ef7d65cac7ff2adc313f7aa0c2b047cab398696e'; patchedSha256='9211dfee775d080c11fda6460cacd78ea0a469c1a2e67dd9bb63397432af3f26'; patchSha256='fadbd9743a2d794a08a47c80a144d0a55bff4eef789c5f7250d93cc18d35c4df'}
)
function Assert-NoReparse([string]$path) {
    $candidate=[IO.Path]::GetFullPath($path)
    if ($candidate -ne $root -and
        -not $candidate.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Device fence path escapes project root'
    }
    while ($candidate) {
        $item=Get-Item -LiteralPath $candidate -Force -ErrorAction SilentlyContinue
        if ($null -ne $item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Device fence input crosses a reparse point'
        }
        $parent=Split-Path $candidate -Parent
        if ($parent -eq $candidate) {break}
        $candidate=$parent
    }
}
foreach ($path in @($manifestPath,'dependencies.lock.json','scripts/apply-device-fence-patch.ps1')) {
    Assert-NoReparse (Join-Path $root $path)
}
$manifest=Get-Content -LiteralPath (Join-Path $root $manifestPath) -Raw | ConvertFrom-Json
$lock=Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$dependency=@($lock.dependencies | Where-Object name -CEQ 'tracktion_engine')
if ($manifest.schema -ne 1 -or $dependency.Count -ne 1 -or $manifest.dependency -cne 'tracktion_engine' -or
    $manifest.revision -cne $revision -or $manifest.sourceDirectory -cne $directory -or
    $manifest.archiveSha256 -cne $archive -or $manifest.license -cne 'GPL-3.0-or-later' -or
    $manifest.controlPreparationFence -isnot [bool] -or $manifest.controlPreparationFence -ne $true -or
    $null -ne $manifest.productionEnabled -or $null -ne $manifest.appliesAfter -or
    $dependency[0].revision -cne $revision -or $dependency[0].sourceDirectory -cne $directory -or
    $dependency[0].sha256 -cne $archive) {
    throw 'Device fence pinned dependency contract mismatch; no source changed'
}
if ($manifest.files.Count -ne $approved.Count -or
    @($manifest.files.path | Select-Object -Unique).Count -ne $approved.Count) {
    throw 'Device fence target set mismatch; no source changed'
}
if ($manifest.vendorReadDependencies.Count -ne 0 -or $null -ne $manifest.sourceDependency -or $null -ne $manifest.sourceDependencies) {
    throw 'Device fence unexpected vendor/app dependency contract; no source changed'
}
$relative='vendor/source/'+$directory
$source=[IO.Path]::GetFullPath((Join-Path $root $relative))
Assert-NoReparse $source
$changes=@()
$pending=@()
$scratchParent=[IO.Path]::GetFullPath((Join-Path $root 'build/device-fence-check/preflight'))
$scratchRelative='build/device-fence-check/preflight/'+[Guid]::NewGuid().ToString('N')
$scratch=[IO.Path]::GetFullPath((Join-Path $root $scratchRelative))
Assert-NoReparse $scratch
Push-Location $root
try {
    # Validate every live target and payload before creating any mirror or writing source.
    foreach ($entry in $manifest.files) {
        $expected=@($approved | Where-Object path -CEQ $entry.path)
        if ($expected.Count -ne 1 -or
            $entry.patch -cne ('patches/tracktion-device-fence/'+[IO.Path]::GetFileName($entry.path)+'.patch') -or
            $entry.originalSha256 -cne $expected[0].originalSha256 -or
            $entry.patchedSha256 -cne $expected[0].patchedSha256 -or
            $entry.patchSha256 -cne $expected[0].patchSha256) {
            throw 'Device fence exact target identity/path mismatch; no source changed'
        }
        $target=[IO.Path]::GetFullPath((Join-Path $source $entry.path))
        Assert-NoReparse $target
        if (-not $target.StartsWith($source+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Device fence target escapes pinned source'
        }
        $patch=Join-Path $root $entry.patch
        Assert-NoReparse $patch
        if ((Get-FileHash -LiteralPath $patch).Hash.ToLowerInvariant() -cne $entry.patchSha256) {
            throw 'Device fence patch payload identity mismatch; no source changed'
        }
        $headers=@(Get-Content -LiteralPath $patch | Where-Object {$_ -match '^(---|\+\+\+) '})
        if ($headers.Count -ne 2 -or $headers[0] -cne ('--- a/'+$entry.path) -or
            $headers[1] -cne ('+++ b/'+$entry.path)) {
            throw 'Device fence patch header mismatch; no source changed'
        }
        $stats=@(& git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --numstat $patch)
        if ($LASTEXITCODE -ne 0 -or $stats.Count -ne 1 -or
            $stats[0] -cnotmatch ('^\d+\t\d+\t'+[regex]::Escape($entry.path)+'$')) {
            throw 'Device fence patch changes unexpected or binary target'
        }
        $actual=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()
        # Exact DeviceManager successors require the complete canonical chain,
        # including every ancestor/payload/source input, before any skip or write.
        if ($entry.path -cin @('modules/tracktion_engine/playback/tracktion_DeviceManager.h',
                              'modules/tracktion_engine/playback/tracktion_DeviceManager.cpp') -and
            $actual -cne $entry.originalSha256 -and $actual -cne $entry.patchedSha256) {
            Assert-NoReparse $target
            Assert-NoReparse (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor $manifestPath $entry.path $actual
            if ($LASTEXITCODE -ne 0) {throw 'Device fence canonical successor verification failed; no source changed'}
            continue
        }
        $successor=$null
        $localSuccessor=$null
        if ($actual -cne $entry.originalSha256 -and $actual -cne $entry.patchedSha256) {
            $admissionPath='patches/tracktion-source-admission/manifest.json'
            Assert-NoReparse (Join-Path $root $admissionPath)
            $admission=Get-Content -LiteralPath (Join-Path $root $admissionPath) -Raw | ConvertFrom-Json
            $following=@($admission.files | Where-Object path -CEQ $entry.path)
            if ($following.Count -ne 1) {throw 'Missing exact strict source-admission predecessor'}
            if ($following[0].patchedSha256 -cne $actual) {
                $localPath='patches/tracktion-local-source-initial/manifest.json'
                Assert-NoReparse (Join-Path $root $localPath)
                $local=Get-Content -LiteralPath (Join-Path $root $localPath) -Raw | ConvertFrom-Json
                $localFollowing=@($local.files | Where-Object path -CEQ $entry.path)
                if ($local.appliesAfter -cne $admissionPath -or $localFollowing.Count -ne 1 -or
                    $localFollowing[0].originalSha256 -cne $following[0].patchedSha256 -or
                    $localFollowing[0].patchedSha256 -cne $actual) {
                    throw 'Unrecognized device fence source preserved; no source changed'
                }
                $localSuccessor=$localFollowing[0]
            }
            Assert-NoReparse (Join-Path $PSScriptRoot 'guard-structural-graph.py')
            & python (Join-Path $PSScriptRoot 'guard-structural-graph.py') --known-successor $manifestPath $entry.path $actual
            if ($LASTEXITCODE -ne 0) {throw 'Device fence source-admission successor validation failed; no source changed'}
            $successor=$following[0]
        }
        $changes+=@{entry=$entry; target=$target; patch=$patch; initial=$actual; applied=($actual -ceq $entry.patchedSha256 -or $null -ne $successor); successor=$successor; localSuccessor=$localSuccessor}
        if ($actual -ceq $entry.originalSha256) {$pending+=$patch}
    }
    # Reverse already-applied inputs in a disposable mirror to prove originals,
    # then apply the device fence patch there and check every exact raw LF post-image.
    foreach ($change in $changes) {
        $copy=Join-Path $scratch $change.entry.path
        New-Item -ItemType Directory -Path (Split-Path $copy -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $change.target -Destination $copy
        if ($null -ne $change.localSuccessor) {
            # Reverse exactly LOCAL -> strict source -> fence, never pretend
            # that a two-stage successor has a direct fence preimage.
            $localPatch=Join-Path $root $change.localSuccessor.patch
            Assert-NoReparse $localPatch
            if ((Get-FileHash -LiteralPath $localPatch).Hash.ToLowerInvariant() -cne $change.localSuccessor.patchSha256) {
                throw 'Device fence LOCAL-CLOSED payload changed; no source changed'
            }
            & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --reverse --directory=$scratchRelative $localPatch
            if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant() -cne $change.successor.patchedSha256) {
                throw 'Device fence LOCAL-CLOSED reverse mismatch; no source changed'
            }
        }
        if ($null -ne $change.successor) {
            # Undo only the fully validated successor in this disposable mirror
            # BEFORE proving the genuine original CONTROL-fence preimage.
            $followingPatch=Join-Path $root $change.successor.patch
            Assert-NoReparse $followingPatch
            if ((Get-FileHash -LiteralPath $followingPatch).Hash.ToLowerInvariant() -cne $change.successor.patchSha256) {
                throw 'Device fence validated successor payload changed; no source changed'
            }
            & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --reverse --directory=$scratchRelative $followingPatch
            if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant() -cne $change.entry.patchedSha256) {
                throw 'Device fence successor mirror reverse mismatch; no source changed'
            }
        }
        if ($change.applied) {
            & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --reverse --directory=$scratchRelative $change.patch
            if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant() -cne $change.entry.originalSha256) {
                throw 'Device fence reverse preimage mismatch; no dependency source changed'
            }
        }
    }
    if ($changes.Count -gt 0) {
        $patches=@($changes | ForEach-Object {$_.patch})
        $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                     'apply',"--directory=$scratchRelative")+$patches
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'Device fence isolated combined preflight failed; no dependency source changed'}
    }
    foreach ($change in $changes) {
        if ((Get-FileHash -LiteralPath (Join-Path $scratch $change.entry.path)).Hash.ToLowerInvariant() -cne $change.entry.patchedSha256) {
            throw 'Device fence declared raw post-image mismatch; no dependency source changed'
        }
    }
    if (-not $VerifyOnly -and $pending.Count -gt 0) {
        foreach ($change in $changes) {
            if ((Get-FileHash -LiteralPath $change.target).Hash.ToLowerInvariant() -cne $change.initial) {
                throw 'Device fence input changed during preflight; source preserved'
            }
        }
        $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                     'apply','--check',"--directory=$relative")+$pending
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'Device fence final preflight failed; no dependency source changed'}
        $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                     'apply',"--directory=$relative")+$pending
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'Device fence patch application failed'}
        foreach ($change in $changes) {
            $expected=if($null -ne $change.successor){$change.initial}else{$change.entry.patchedSha256}
            if ((Get-FileHash -LiteralPath $change.target).Hash.ToLowerInvariant() -cne $expected) {
                throw 'Device fence final raw identity mismatch'
            }
        }
    }
    Write-Output 'Verified pinned optional CONTROL device preparation fence; exact raw identities.'
} finally {
    Pop-Location
    if (-not $scratch.StartsWith($scratchParent+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Device fence scratch cleanup escaped its task directory'
    }
    Assert-NoReparse $scratch
    if (Test-Path -LiteralPath $scratch) {Remove-Item -LiteralPath $scratch -Recurse -Force}
}
