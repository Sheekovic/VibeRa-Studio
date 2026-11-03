param([switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$manifestPath='patches/juce-number-safety/manifest.json'
$revision='37c894f83d379179b2070d437ccd0f1cd9af9576'
$directory='JUCE-'+$revision
$archive='f3da0cce79f1008b02ba1cddde8eba1087d07a4d6a775a0ba0d56a9d3cecd18a'
# These reviewed raw identities deliberately agree with the checked-in manifest.
# A changed contract requires an explicit source/payload review of the one target and public header.
$approved=@(
    @{path='modules/juce_core/text/juce_String.cpp'; originalSha256='707f850eeccaa85dbbd80e44e0ecce6202847431c93d533d701c03079c65da91'; patchedSha256='859e823bd6db75554664e705243a7ade8ba2b41c5ac3f29acec429feee8c5c64'; patchSha256='b6d6148ce7f8f333711642d3630f7b8545b40678e2cc896e065719af51fdaf1a'}
)
$publicHeader='modules/juce_core/text/juce_String.h'
$publicHeaderSha256='d19d8d16dee7f20a8c97ea1c3b3f38e18f6e6213679ae30abb787a02c57fdc40'
function Assert-NoReparse([string]$path) {
    $candidate=[IO.Path]::GetFullPath($path)
    if ($candidate -ne $root -and
        -not $candidate.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Number safety path escapes project root'
    }
    while ($candidate) {
        $item=Get-Item -LiteralPath $candidate -Force -ErrorAction SilentlyContinue
        if ($null -ne $item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Number safety input crosses a reparse point'
        }
        $parent=Split-Path $candidate -Parent
        if ($parent -eq $candidate) {break}
        $candidate=$parent
    }
}
foreach ($path in @($manifestPath,'dependencies.lock.json','scripts/apply-number-safety-patch.ps1')) {
    Assert-NoReparse (Join-Path $root $path)
}
$manifest=Get-Content -LiteralPath (Join-Path $root $manifestPath) -Raw | ConvertFrom-Json
$lock=Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$dependency=@($lock.dependencies | Where-Object name -CEQ 'JUCE')
if ($manifest.schema -ne 1 -or $dependency.Count -ne 1 -or $manifest.dependency -cne 'JUCE' -or
    $manifest.revision -cne $revision -or $manifest.sourceDirectory -cne $directory -or
    $manifest.archiveSha256 -cne $archive -or $manifest.license -cne 'AGPL-3.0-only' -or
    $manifest.defaultExceptionSafety -isnot [bool] -or $manifest.defaultExceptionSafety -ne $true -or
    $null -ne $manifest.productionEnabled -or $null -ne $manifest.appliesAfter -or
    $dependency[0].revision -cne $revision -or $dependency[0].sourceDirectory -cne $directory -or
    $dependency[0].sha256 -cne $archive) {
    throw 'Number safety pinned dependency contract mismatch; no source changed'
}
if ($manifest.files.Count -ne $approved.Count -or
    @($manifest.files.path | Select-Object -Unique).Count -ne $approved.Count) {
    throw 'Number safety target set mismatch; no source changed'
}
if ($manifest.vendorReadDependencies.Count -ne 1 -or
    $manifest.vendorReadDependencies[0].path -cne $publicHeader -or
    $manifest.vendorReadDependencies[0].mode -cne 'raw' -or
    $manifest.vendorReadDependencies[0].sha256 -cne $publicHeaderSha256 -or
    $null -ne $manifest.sourceDependency -or $null -ne $manifest.sourceDependencies) {
    throw 'Number safety public header/default-scope contract mismatch; no source changed'
}
$relative='vendor/source/'+$directory
$source=[IO.Path]::GetFullPath((Join-Path $root $relative))
Assert-NoReparse $source
$publicHeaderPath=[IO.Path]::GetFullPath((Join-Path $source $publicHeader))
Assert-NoReparse $publicHeaderPath
if ((Get-FileHash -LiteralPath $publicHeaderPath).Hash.ToLowerInvariant() -cne $publicHeaderSha256) {
    throw 'Number safety public String header identity mismatch; no source changed'
}
$changes=@()
$pending=@()
$scratchParent=[IO.Path]::GetFullPath((Join-Path $root 'build/number-safety-check/preflight'))
$scratchRelative='build/number-safety-check/preflight/'+[Guid]::NewGuid().ToString('N')
$scratch=[IO.Path]::GetFullPath((Join-Path $root $scratchRelative))
Assert-NoReparse $scratch
Push-Location $root
try {
    # Validate every live target and payload before creating any mirror or writing source.
    foreach ($entry in $manifest.files) {
        $expected=@($approved | Where-Object path -CEQ $entry.path)
        if ($expected.Count -ne 1 -or
            $entry.patch -cne ('patches/juce-number-safety/'+[IO.Path]::GetFileName($entry.path)+'.patch') -or
            $entry.originalSha256 -cne $expected[0].originalSha256 -or
            $entry.patchedSha256 -cne $expected[0].patchedSha256 -or
            $entry.patchSha256 -cne $expected[0].patchSha256) {
            throw 'Number safety exact target identity/path mismatch; no source changed'
        }
        $target=[IO.Path]::GetFullPath((Join-Path $source $entry.path))
        Assert-NoReparse $target
        if (-not $target.StartsWith($source+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Number safety target escapes pinned source'
        }
        $patch=Join-Path $root $entry.patch
        Assert-NoReparse $patch
        if ((Get-FileHash -LiteralPath $patch).Hash.ToLowerInvariant() -cne $entry.patchSha256) {
            throw 'Number safety patch payload identity mismatch; no source changed'
        }
        $headers=@(Get-Content -LiteralPath $patch | Where-Object {$_ -match '^(---|\+\+\+) '})
        if ($headers.Count -ne 2 -or $headers[0] -cne ('--- a/'+$entry.path) -or
            $headers[1] -cne ('+++ b/'+$entry.path)) {
            throw 'Number safety patch header mismatch; no source changed'
        }
        $stats=@(& git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --numstat $patch)
        if ($LASTEXITCODE -ne 0 -or $stats.Count -ne 1 -or
            $stats[0] -cnotmatch ('^\d+\t\d+\t'+[regex]::Escape($entry.path)+'$')) {
            throw 'Number safety patch changes unexpected or binary target'
        }
        $actual=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()
        if ($actual -cne $entry.originalSha256 -and $actual -cne $entry.patchedSha256) {
            throw 'Unrecognized number safety source preserved; no source changed'
        }
        $changes+=@{entry=$entry; target=$target; patch=$patch; initial=$actual; applied=($actual -ceq $entry.patchedSha256)}
        if ($actual -ceq $entry.originalSha256) {$pending+=$patch}
    }
    # Reverse already-applied inputs in a disposable mirror to prove originals,
    # then apply the number safety patch there and check every exact raw CRLF post-image.
    foreach ($change in $changes) {
        $copy=Join-Path $scratch $change.entry.path
        New-Item -ItemType Directory -Path (Split-Path $copy -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $change.target -Destination $copy
        if ($change.applied) {
            & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --reverse --directory=$scratchRelative $change.patch
            if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant() -cne $change.entry.originalSha256) {
                throw 'Number safety reverse preimage mismatch; no dependency source changed'
            }
        }
    }
    $patches=@($changes | ForEach-Object {$_.patch})
    $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                 'apply',"--directory=$scratchRelative")+$patches
    & git @arguments
    if ($LASTEXITCODE -ne 0) {throw 'Number safety isolated combined preflight failed; no dependency source changed'}
    foreach ($change in $changes) {
        if ((Get-FileHash -LiteralPath (Join-Path $scratch $change.entry.path)).Hash.ToLowerInvariant() -cne $change.entry.patchedSha256) {
            throw 'Number safety declared raw post-image mismatch; no dependency source changed'
        }
    }
    if (-not $VerifyOnly -and $pending.Count -gt 0) {
        Assert-NoReparse $publicHeaderPath
        if ((Get-FileHash -LiteralPath $publicHeaderPath).Hash.ToLowerInvariant() -cne $publicHeaderSha256) {
            throw 'Number safety public header changed during preflight; source preserved'
        }
        foreach ($change in $changes) {
            if ((Get-FileHash -LiteralPath $change.target).Hash.ToLowerInvariant() -cne $change.initial) {
                throw 'Number safety input changed during preflight; source preserved'
            }
        }
        $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                     'apply','--check',"--directory=$relative")+$pending
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'Number safety final preflight failed; no dependency source changed'}
        $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                     'apply',"--directory=$relative")+$pending
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'Number safety patch application failed'}
        foreach ($change in $changes) {
            if ((Get-FileHash -LiteralPath $change.target).Hash.ToLowerInvariant() -cne $change.entry.patchedSha256) {
                throw 'Number safety final raw identity mismatch'
            }
        }
    }
    Write-Output 'Verified pinned default JUCE numeric exception safety; exact raw identities.'
} finally {
    Pop-Location
    if (-not $scratch.StartsWith($scratchParent+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Number safety scratch cleanup escaped its task directory'
    }
    Assert-NoReparse $scratch
    if (Test-Path -LiteralPath $scratch) {Remove-Item -LiteralPath $scratch -Recurse -Force}
}
