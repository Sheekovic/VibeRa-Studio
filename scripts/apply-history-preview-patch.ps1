param([switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$manifestPath='patches/juce-history-preview/manifest.json'
$revision='37c894f83d379179b2070d437ccd0f1cd9af9576'
$directory='JUCE-'+$revision
$archive='f3da0cce79f1008b02ba1cddde8eba1087d07a4d6a775a0ba0d56a9d3cecd18a'
# These reviewed raw identities deliberately agree with the checked-in manifest.
# A changed contract requires an explicit source/payload review of both files.
$approved=@(
    @{path='modules/juce_data_structures/undomanager/juce_UndoManager.h'; originalSha256='c5fe50604433ac74631eacce30d2f5ddeec4d6d6ea692df93d397a0aab9dfbf0'; patchedSha256='44e61c66b9ffed5eddb19232e6323ca0748b474b463d6efb9baf9570708e3a3e'; patchSha256='86c04bb6c0e39c0c47add934e7f4cd2e9baf14bb7379a6875e054067497bbec6'},
    @{path='modules/juce_data_structures/undomanager/juce_UndoManager.cpp'; originalSha256='3c671050294238a8900d53a49db10d1c15c06a03e1dcfae34ab02ff0371b968f'; patchedSha256='f5608a00535ee9897e6b69bf2bb4e84b7fb62cbe6ca1bffdd8a8de46843e8ed6'; patchSha256='13ed78763260e258843b5764be5cd9ac74aca482103e9472ef1005109b5fb371'}
)
function Assert-NoReparse([string]$path) {
    $candidate=[IO.Path]::GetFullPath($path)
    if ($candidate -ne $root -and
        -not $candidate.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'History preview path escapes project root'
    }
    while ($candidate) {
        $item=Get-Item -LiteralPath $candidate -Force -ErrorAction SilentlyContinue
        if ($null -ne $item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'History preview input crosses a reparse point'
        }
        $parent=Split-Path $candidate -Parent
        if ($parent -eq $candidate) {break}
        $candidate=$parent
    }
}
foreach ($path in @($manifestPath,'dependencies.lock.json','scripts/apply-history-preview-patch.ps1')) {
    Assert-NoReparse (Join-Path $root $path)
}
$manifest=Get-Content -LiteralPath (Join-Path $root $manifestPath) -Raw | ConvertFrom-Json
$lock=Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$dependency=@($lock.dependencies | Where-Object name -CEQ 'JUCE')
if ($manifest.schema -ne 1 -or $dependency.Count -ne 1 -or $manifest.dependency -cne 'JUCE' -or
    $manifest.revision -cne $revision -or $manifest.sourceDirectory -cne $directory -or
    $manifest.archiveSha256 -cne $archive -or $manifest.license -cne 'AGPL-3.0-only' -or
    $dependency[0].revision -cne $revision -or $dependency[0].sourceDirectory -cne $directory -or
    $dependency[0].sha256 -cne $archive) {
    throw 'History preview pinned dependency contract mismatch; no source changed'
}
if ($manifest.files.Count -ne $approved.Count -or
    @($manifest.files.path | Select-Object -Unique).Count -ne $approved.Count) {
    throw 'History preview target set mismatch; no source changed'
}
$relative='vendor/source/'+$directory
$source=[IO.Path]::GetFullPath((Join-Path $root $relative))
Assert-NoReparse $source
$changes=@()
$pending=@()
$preparedPath='patches/juce-prepared-history/manifest.json'
$preparedValidated=$false
$preparedManifest=$null
$scratchParent=[IO.Path]::GetFullPath((Join-Path $root 'build/history-preview-check/preflight'))
$scratchRelative='build/history-preview-check/preflight/'+[Guid]::NewGuid().ToString('N')
$scratch=[IO.Path]::GetFullPath((Join-Path $root $scratchRelative))
Assert-NoReparse $scratch
Push-Location $root
try {
    # Validate every live target and payload before creating any mirror or writing source.
    foreach ($entry in $manifest.files) {
        $expected=@($approved | Where-Object path -CEQ $entry.path)
        if ($expected.Count -ne 1 -or
            $entry.patch -cne ('patches/juce-history-preview/'+[IO.Path]::GetFileName($entry.path)+'.patch') -or
            $entry.originalSha256 -cne $expected[0].originalSha256 -or
            $entry.patchedSha256 -cne $expected[0].patchedSha256 -or
            $entry.patchSha256 -cne $expected[0].patchSha256) {
            throw 'History preview exact target identity/path mismatch; no source changed'
        }
        $target=[IO.Path]::GetFullPath((Join-Path $source $entry.path))
        Assert-NoReparse $target
        if (-not $target.StartsWith($source+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
            throw 'History preview target escapes pinned source'
        }
        $patch=Join-Path $root $entry.patch
        Assert-NoReparse $patch
        if ((Get-FileHash -LiteralPath $patch).Hash.ToLowerInvariant() -cne $entry.patchSha256) {
            throw 'History preview patch payload identity mismatch; no source changed'
        }
        $headers=@(Get-Content -LiteralPath $patch | Where-Object {$_ -match '^(---|\+\+\+) '})
        if ($headers.Count -ne 2 -or $headers[0] -cne ('--- a/'+$entry.path) -or
            $headers[1] -cne ('+++ b/'+$entry.path)) {
            throw 'History preview patch header mismatch; no source changed'
        }
        $stats=@(& git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --numstat $patch)
        if ($LASTEXITCODE -ne 0 -or $stats.Count -ne 1 -or
            $stats[0] -cnotmatch ('^\d+\t\d+\t'+[regex]::Escape($entry.path)+'$')) {
            throw 'History preview patch changes unexpected or binary target'
        }
        $actual=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()
        $preparedApplied=$false
        $successor=$null
        if ($actual -cne $entry.originalSha256 -and $actual -cne $entry.patchedSha256) {
            if (Test-Path -LiteralPath (Join-Path $root $preparedPath) -PathType Leaf) {
                Assert-NoReparse (Join-Path $root $preparedPath)
                Assert-NoReparse (Join-Path $PSScriptRoot 'apply-prepared-history-patch.ps1')
                $preparedManifest=Get-Content -LiteralPath (Join-Path $root $preparedPath) -Raw | ConvertFrom-Json
                $following=@($preparedManifest.files | Where-Object path -CEQ $entry.path)
                if ($following.Count -eq 1 -and $following[0].originalSha256 -ceq $entry.patchedSha256 -and
                    $following[0].patchedSha256 -cmatch '^[0-9a-f]{64}$' -and
                    $actual -ceq $following[0].patchedSha256) {
                    if (-not $preparedValidated) {
                        & (Join-Path $PSScriptRoot 'apply-prepared-history-patch.ps1') -VerifyOnly
                        $preparedValidated=$true
                    }
                    $preparedApplied=$true
                    $successor=$following[0]
                }
            }
            if (-not $preparedApplied) {
                throw 'Unrecognized history preview source preserved; no source changed'
            }
        }
        $changes+=@{entry=$entry; target=$target; patch=$patch; initial=$actual;
                    applied=($actual -ceq $entry.patchedSha256 -or $preparedApplied);
                    preparedApplied=$preparedApplied; successor=$successor}
        if ($actual -ceq $entry.originalSha256) {$pending+=$patch}
    }
    # Reverse already-applied inputs in a disposable mirror to prove originals,
    # then apply BOTH patches there and check every exact raw CRLF post-image.
    foreach ($change in $changes) {
        $copy=Join-Path $scratch $change.entry.path
        New-Item -ItemType Directory -Path (Split-Path $copy -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $change.target -Destination $copy
        if ($change.preparedApplied) {
            if (-not $preparedValidated -or $null -eq $change.successor) {
                throw 'Prepared history successor not validated before preview reconstruction'
            }
            & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --reverse --directory=$scratchRelative (Join-Path $root $change.successor.patch)
            if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant() -cne $change.entry.patchedSha256) {
                throw 'Prepared history reverse did not prove exact preview post-image; no dependency source changed'
            }
        }
        if ($change.applied) {
            & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --reverse --directory=$scratchRelative $change.patch
            if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant() -cne $change.entry.originalSha256) {
                throw 'History preview reverse preimage mismatch; no dependency source changed'
            }
        }
    }
    $patches=@($changes | ForEach-Object {$_.patch})
    $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                 'apply',"--directory=$scratchRelative")+$patches
    & git @arguments
    if ($LASTEXITCODE -ne 0) {throw 'History preview isolated combined preflight failed; no dependency source changed'}
    foreach ($change in $changes) {
        if ((Get-FileHash -LiteralPath (Join-Path $scratch $change.entry.path)).Hash.ToLowerInvariant() -cne $change.entry.patchedSha256) {
            throw 'History preview declared raw post-image mismatch; no dependency source changed'
        }
    }
    if (-not $VerifyOnly -and $pending.Count -gt 0) {
        foreach ($change in $changes) {
            if ((Get-FileHash -LiteralPath $change.target).Hash.ToLowerInvariant() -cne $change.initial) {
                throw 'History preview input changed during preflight; source preserved'
            }
        }
        $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                     'apply','--check',"--directory=$relative")+$pending
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'History preview final preflight failed; no dependency source changed'}
        $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                     'apply',"--directory=$relative")+$pending
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'History preview patch application failed'}
        foreach ($change in $changes) {
            $expected=if($change.preparedApplied){$change.successor.patchedSha256}else{$change.entry.patchedSha256}
            if ((Get-FileHash -LiteralPath $change.target).Hash.ToLowerInvariant() -cne $expected) {
                throw 'History preview final raw identity mismatch'
            }
        }
    }
    Write-Output 'Verified pinned JUCE history-preview images and explicit prepared successor; exact raw identities.'
} finally {
    Pop-Location
    if (-not $scratch.StartsWith($scratchParent+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'History preview scratch cleanup escaped its task directory'
    }
    Assert-NoReparse $scratch
    if (Test-Path -LiteralPath $scratch) {Remove-Item -LiteralPath $scratch -Recurse -Force}
}
