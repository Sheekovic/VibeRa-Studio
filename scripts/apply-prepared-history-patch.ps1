param([switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$manifestPath='patches/juce-prepared-history/manifest.json'
$predecessorPath='patches/juce-history-preview/manifest.json'
$revision='37c894f83d379179b2070d437ccd0f1cd9af9576'
$directory='JUCE-'+$revision
$archive='f3da0cce79f1008b02ba1cddde8eba1087d07a4d6a775a0ba0d56a9d3cecd18a'
# These reviewed raw identities deliberately agree with the checked-in manifest.
# A changed contract requires an explicit source/payload review of both files.
$approved=@(
    @{path='modules/juce_data_structures/undomanager/juce_UndoManager.h'; originalSha256='44e61c66b9ffed5eddb19232e6323ca0748b474b463d6efb9baf9570708e3a3e'; patchedSha256='06b051f0b2e931676e0a38368a1cf9dc4a79af402dfd43c4260d6f5b5b5cedaf'; patchSha256='9fb8ac45fa7710fe5b4b0918644364121699861379eaaf07117454f2fddddb62'},
    @{path='modules/juce_data_structures/undomanager/juce_UndoManager.cpp'; originalSha256='f5608a00535ee9897e6b69bf2bb4e84b7fb62cbe6ca1bffdd8a8de46843e8ed6'; patchedSha256='80ad7d9f71b6a01c4a794d453c1110b0af5c25dbdf27863fefd4ca62654088ae'; patchSha256='d7d0399982531040677c9c1020f24853843603e61ca002491a313ef5cadae9a4'}
)
function Assert-NoReparse([string]$path) {
    $candidate=[IO.Path]::GetFullPath($path)
    if ($candidate -ne $root -and
        -not $candidate.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Prepared history path escapes project root'
    }
    while ($candidate) {
        $item=Get-Item -LiteralPath $candidate -Force -ErrorAction SilentlyContinue
        if ($null -ne $item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Prepared history input crosses a reparse point'
        }
        $parent=Split-Path $candidate -Parent
        if ($parent -eq $candidate) {break}
        $candidate=$parent
    }
}
foreach ($path in @($manifestPath,$predecessorPath,'dependencies.lock.json','scripts/apply-prepared-history-patch.ps1')) {
    Assert-NoReparse (Join-Path $root $path)
}
$manifest=Get-Content -LiteralPath (Join-Path $root $manifestPath) -Raw | ConvertFrom-Json
$predecessor=Get-Content -LiteralPath (Join-Path $root $predecessorPath) -Raw | ConvertFrom-Json
$lock=Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$dependency=@($lock.dependencies | Where-Object name -CEQ 'JUCE')
if ($manifest.schema -ne 1 -or $dependency.Count -ne 1 -or $manifest.dependency -cne 'JUCE' -or
    $manifest.revision -cne $revision -or $manifest.sourceDirectory -cne $directory -or
    $manifest.archiveSha256 -cne $archive -or $manifest.license -cne 'AGPL-3.0-only' -or
    $manifest.appliesAfter -cne $predecessorPath -or
    $manifest.productionEnabled -isnot [bool] -or $manifest.productionEnabled -ne $false -or
    $predecessor.schema -ne 1 -or $predecessor.dependency -cne 'JUCE' -or
    $predecessor.revision -cne $revision -or $predecessor.sourceDirectory -cne $directory -or
    $predecessor.archiveSha256 -cne $archive -or $predecessor.license -cne 'AGPL-3.0-only' -or
    $dependency[0].revision -cne $revision -or $dependency[0].sourceDirectory -cne $directory -or
    $dependency[0].sha256 -cne $archive) {
    throw 'Prepared history pinned dependency contract mismatch; no source changed'
}
if ($manifest.files.Count -ne $approved.Count -or
    @($manifest.files.path | Select-Object -Unique).Count -ne $approved.Count) {
    throw 'Prepared history target set mismatch; no source changed'
}
if ($predecessor.files.Count -ne $approved.Count -or
    @($predecessor.files.path | Select-Object -Unique).Count -ne $approved.Count) {
    throw 'Prepared history predecessor target set mismatch; no source changed'
}
foreach ($expected in $approved) {
    $prior=@($predecessor.files | Where-Object path -CEQ $expected.path)
    if ($prior.Count -ne 1 -or $prior[0].patchedSha256 -cne $expected.originalSha256) {
        throw 'Prepared history exact preview predecessor mismatch; no source changed'
    }
}
# This pinned generic JUCE API includes no application header. It cannot certify
# Controller StateAction ownership or noexcept callback semantics; root owns those gates.
if ($null -ne $manifest.sourceDependencies -or $null -ne $manifest.sourceDependency) {
    throw 'Prepared history unexpected application source dependency contract'
}
$relative='vendor/source/'+$directory
$source=[IO.Path]::GetFullPath((Join-Path $root $relative))
Assert-NoReparse $source
$changes=@()
$pending=@()
$scratchParent=[IO.Path]::GetFullPath((Join-Path $root 'build/prepared-history-check/preflight'))
$scratchRelative='build/prepared-history-check/preflight/'+[Guid]::NewGuid().ToString('N')
$scratch=[IO.Path]::GetFullPath((Join-Path $root $scratchRelative))
Assert-NoReparse $scratch
Push-Location $root
try {
    # Validate every live target and payload before creating any mirror or writing source.
    foreach ($entry in $manifest.files) {
        $expected=@($approved | Where-Object path -CEQ $entry.path)
        if ($expected.Count -ne 1 -or
            $entry.patch -cne ('patches/juce-prepared-history/'+[IO.Path]::GetFileName($entry.path)+'.patch') -or
            $entry.originalSha256 -cne $expected[0].originalSha256 -or
            $entry.patchedSha256 -cne $expected[0].patchedSha256 -or
            $entry.patchSha256 -cne $expected[0].patchSha256) {
            throw 'Prepared history exact target identity/path mismatch; no source changed'
        }
        $target=[IO.Path]::GetFullPath((Join-Path $source $entry.path))
        Assert-NoReparse $target
        if (-not $target.StartsWith($source+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Prepared history target escapes pinned source'
        }
        $patch=Join-Path $root $entry.patch
        Assert-NoReparse $patch
        if ((Get-FileHash -LiteralPath $patch).Hash.ToLowerInvariant() -cne $entry.patchSha256) {
            throw 'Prepared history patch payload identity mismatch; no source changed'
        }
        $headers=@(Get-Content -LiteralPath $patch | Where-Object {$_ -match '^(---|\+\+\+) '})
        if ($headers.Count -ne 2 -or $headers[0] -cne ('--- a/'+$entry.path) -or
            $headers[1] -cne ('+++ b/'+$entry.path)) {
            throw 'Prepared history patch header mismatch; no source changed'
        }
        $stats=@(& git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --numstat $patch)
        if ($LASTEXITCODE -ne 0 -or $stats.Count -ne 1 -or
            $stats[0] -cnotmatch ('^\d+\t\d+\t'+[regex]::Escape($entry.path)+'$')) {
            throw 'Prepared history patch changes unexpected or binary target'
        }
        $actual=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()
        if ($actual -cne $entry.originalSha256 -and $actual -cne $entry.patchedSha256) {
            throw 'Unrecognized prepared history source preserved; no source changed'
        }
        $changes+=@{entry=$entry; target=$target; patch=$patch; initial=$actual; applied=($actual -ceq $entry.patchedSha256)}
        if ($actual -ceq $entry.originalSha256) {$pending+=$patch}
    }
    # Reverse already-applied inputs in a disposable mirror to prove originals,
    # then apply BOTH patches there and check every exact raw post-image.
    foreach ($change in $changes) {
        $copy=Join-Path $scratch $change.entry.path
        New-Item -ItemType Directory -Path (Split-Path $copy -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $change.target -Destination $copy
        if ($change.applied) {
            & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --reverse --directory=$scratchRelative $change.patch
            if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant() -cne $change.entry.originalSha256) {
                throw 'Prepared history reverse preimage mismatch; no dependency source changed'
            }
        }
    }
    $patches=@($changes | ForEach-Object {$_.patch})
    $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                 'apply',"--directory=$scratchRelative")+$patches
    & git @arguments
    if ($LASTEXITCODE -ne 0) {throw 'Prepared history isolated combined preflight failed; no dependency source changed'}
    foreach ($change in $changes) {
        if ((Get-FileHash -LiteralPath (Join-Path $scratch $change.entry.path)).Hash.ToLowerInvariant() -cne $change.entry.patchedSha256) {
            throw 'Prepared history declared raw post-image mismatch; no dependency source changed'
        }
    }
    if (-not $VerifyOnly -and $pending.Count -gt 0) {
        foreach ($change in $changes) {
            if ((Get-FileHash -LiteralPath $change.target).Hash.ToLowerInvariant() -cne $change.initial) {
                throw 'Prepared history input changed during preflight; source preserved'
            }
        }
        $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                     'apply','--check',"--directory=$relative")+$pending
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'Prepared history final preflight failed; no dependency source changed'}
        $arguments=@('-c',"safe.directory=$root",'-c','core.autocrlf=false','-c','core.eol=lf',
                     'apply',"--directory=$relative")+$pending
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'Prepared history patch application failed'}
        foreach ($change in $changes) {
            if ((Get-FileHash -LiteralPath $change.target).Hash.ToLowerInvariant() -cne $change.entry.patchedSha256) {
                throw 'Prepared history final raw identity mismatch'
            }
        }
    }
    Write-Output 'Verified pinned JUCE prepared-history successor; production remains OFF; exact raw source identities.'
} finally {
    Pop-Location
    if (-not $scratch.StartsWith($scratchParent+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Prepared history scratch cleanup escaped its task directory'
    }
    Assert-NoReparse $scratch
    if (Test-Path -LiteralPath $scratch) {Remove-Item -LiteralPath $scratch -Recurse -Force}
}
