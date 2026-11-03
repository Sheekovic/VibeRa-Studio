param([switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
function Assert-NoReparse([string]$path) {
    $candidate=[IO.Path]::GetFullPath($path)
    if ($candidate -ne $root -and
        -not $candidate.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Native editor bridge path escapes project root'
    }
    while ($candidate) {
        if ((Test-Path -LiteralPath $candidate) -and
            ((Get-Item -LiteralPath $candidate -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Native editor bridge input crosses a reparse point'
        }
        $parent=Split-Path $candidate -Parent
        if ($parent -eq $candidate) {break}
        $candidate=$parent
    }
}
foreach ($path in @('patches/juce-vst3-editor/manifest.json','dependencies.lock.json',
                    'patches/juce-vst3-editor/juce_VST3PluginFormat.cpp.patch')) {
    Assert-NoReparse (Join-Path $root $path)
}
$manifest=Get-Content -LiteralPath (Join-Path $root 'patches/juce-vst3-editor/manifest.json') -Raw | ConvertFrom-Json
$lock=Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$dependency=@($lock.dependencies | Where-Object name -eq 'JUCE')
$revision='37c894f83d379179b2070d437ccd0f1cd9af9576'
$directory='JUCE-'+$revision
$archive='f3da0cce79f1008b02ba1cddde8eba1087d07a4d6a775a0ba0d56a9d3cecd18a'
if ($manifest.schema -ne 1 -or $dependency.Count -ne 1 -or $manifest.dependency -cne 'JUCE' -or
    $manifest.revision -cne $revision -or $manifest.sourceDirectory -cne $directory -or
    $manifest.archiveSha256 -cne $archive -or $dependency[0].revision -cne $revision -or
    $dependency[0].sourceDirectory -cne $directory -or $dependency[0].sha256 -cne $archive) {
    throw 'Native editor bridge pin mismatch; no source changed'
}
$allowed='modules/juce_audio_processors/format_types/juce_VST3PluginFormat.cpp'
if ($manifest.files.Count -ne 1) {throw 'Unexpected native editor bridge target count'}
$entry=$manifest.files[0]
if ($entry.path -cne $allowed -or $entry.patch -cne 'patches/juce-vst3-editor/juce_VST3PluginFormat.cpp.patch' -or
    $entry.originalSha256 -cnotmatch '^[0-9a-f]{64}$' -or $entry.patchedSha256 -cnotmatch '^[0-9a-f]{64}$' -or
    $entry.patchSha256 -cnotmatch '^[0-9a-f]{64}$') {throw 'Unexpected native editor bridge identity/path'}
$relative='vendor/source/'+$directory
$source=[IO.Path]::GetFullPath((Join-Path $root $relative))
$target=[IO.Path]::GetFullPath((Join-Path $source $allowed))
Assert-NoReparse $source
Assert-NoReparse $target
if (-not $target.StartsWith($source+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Native editor bridge escapes pinned source'
}
$patch=Join-Path $root $entry.patch
if ((Get-FileHash -LiteralPath $patch).Hash.ToLowerInvariant() -cne $entry.patchSha256) {
    throw 'Native editor bridge patch bytes changed'
}
$headers=@(Get-Content -LiteralPath $patch | Where-Object {$_ -match '^(---|\+\+\+) '})
if ($headers.Count -ne 2 -or $headers[0] -cne ('--- a/'+$allowed) -or $headers[1] -cne ('+++ b/'+$allowed)) {
    throw 'Native editor bridge patch contains unapproved paths'
}
Push-Location $root
try {
    $stats=@(& git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --numstat $patch)
    if ($LASTEXITCODE -ne 0 -or $stats.Count -ne 1 -or $stats[0] -cnotmatch ('^\d+\t\d+\t'+[regex]::Escape($allowed)+'$')) {
        throw 'Native editor bridge patch changes unexpected/binary target'
    }
    $hash=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()
    if ($hash -ceq $entry.patchedSha256) {
        Write-Output 'Verified already applied pinned native editor attachment bridge.'
        return
    }
    if ($hash -cne $entry.originalSha256) {throw 'Unknown native editor vendor bytes preserved; no source changed'}
    & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --check --directory=$relative $patch
    if ($LASTEXITCODE -ne 0) {throw 'Native editor bridge patch preflight failed; no source changed'}
    # Prove the exact declared post-image before changing the pinned dependency.
    $scratchParent=[IO.Path]::GetFullPath((Join-Path $root 'build/vst3-editor-check/preflight'))
    $scratchRelative='build/vst3-editor-check/preflight/'+[Guid]::NewGuid().ToString('N')
    $scratch=[IO.Path]::GetFullPath((Join-Path $root $scratchRelative))
    Assert-NoReparse $scratch
    $copy=Join-Path $scratch $allowed
    try {
        New-Item -ItemType Directory -Path (Split-Path $copy -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $target -Destination $copy
        & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --directory=$scratchRelative $patch
        if ($LASTEXITCODE -ne 0 -or
            (Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant() -cne $entry.patchedSha256) {
            throw 'Native editor bridge declared post-image mismatch; no dependency source changed'
        }
    } finally {
        if (-not $scratch.StartsWith($scratchParent+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Native editor bridge scratch cleanup escaped its task directory'
        }
        Assert-NoReparse $scratch
        if (Test-Path -LiteralPath $scratch) {Remove-Item -LiteralPath $scratch -Recurse -Force}
    }
    if ($VerifyOnly) {
        Write-Output 'Verified native editor attachment bridge inputs; no source changed.'
        return
    }
    & git -c "safe.directory=$root" -c core.autocrlf=false -c core.eol=lf apply --directory=$relative $patch
    if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant() -cne $entry.patchedSha256) {
        throw 'Native editor attachment bridge application failed'
    }
    Write-Output 'Verified pinned JUCE native VST3 attachment result bridge.'
} finally {Pop-Location}
