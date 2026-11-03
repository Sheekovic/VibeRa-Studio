param([switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$manifestPath='patches/juce-physical-callback/manifest.json'
$revision='37c894f83d379179b2070d437ccd0f1cd9af9576'
$directory='JUCE-'+$revision
$archive='f3da0cce79f1008b02ba1cddde8eba1087d07a4d6a775a0ba0d56a9d3cecd18a'
# These reviewed raw identities deliberately agree with the checked-in manifest.
# Contract changes require review of both raw targets and three native source proofs.
$approved=@(
    @{path='modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.h'; originalSha256='75cabf1ac9731d0bf591a4bf871693bfdf9a607355d4525a20a09ea378ef3148'; patchedSha256='31f77bb0fdd9727450308e3604b14efb43b0790b32943210e6d6f56fd97bede0'; patchSha256='1219fb0040ebd31fde54b997b2d14b71b3ea74cd9dfdeb702b0a7ea765bd24c0'},
    @{path='modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp'; originalSha256='e82d3e9c3f52d4701f89a13e4f84d915560c5c9189d82d31268eb177e47036ea'; patchedSha256='fed6e7ed924bc764d775076be98000c0c853ad53ce21ed31e472d3b9fa1b543c'; patchSha256='3569877c4b7f5d65839a0b4f444eed10d3e00f2fa9f4da159495e49491f4fe7c'}
)
$readApproved=@(
    @{path='modules/juce_audio_devices/native/juce_ASIO_windows.cpp'; mode='raw'; sha256='eac95f405676c35945164b2c4f8a278099ae51143b68579fefb1902450b54489'},
    @{path='modules/juce_audio_devices/native/juce_WASAPI_windows.cpp'; mode='raw'; sha256='a7eb67c4d738706565325855ec3fa835a45d9bf8db40c56c96501a06e52eadc9'},
    @{path='modules/juce_audio_devices/native/juce_DirectSound_windows.cpp'; mode='raw'; sha256='f651edb764085690e90a71830bd9e738ce19a6ce3e391069d6a8f72b84e97638'}
)
function Assert-NoReparse([string]$path) {
    $candidate=[IO.Path]::GetFullPath($path)
    if ($candidate -ne $root -and
        -not $candidate.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Physical callback path escapes project root'
    }
    while ($candidate) {
        $item=Get-Item -LiteralPath $candidate -Force -ErrorAction SilentlyContinue
        if ($null -ne $item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Physical callback input crosses a reparse point'
        }
        $parent=Split-Path $candidate -Parent
        if ($parent -eq $candidate) {break}
        $candidate=$parent
    }
}
foreach ($path in @($manifestPath,'dependencies.lock.json','scripts/apply-physical-callback-patch.ps1')) {
    Assert-NoReparse (Join-Path $root $path)
}
$manifest=Get-Content -LiteralPath (Join-Path $root $manifestPath) -Raw | ConvertFrom-Json
$lock=Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$dependency=@($lock.dependencies | Where-Object name -CEQ 'JUCE')
if ($manifest.schema -ne 1 -or $dependency.Count -ne 1 -or $manifest.dependency -cne 'JUCE' -or
    $manifest.revision -cne $revision -or $manifest.sourceDirectory -cne $directory -or
    $manifest.archiveSha256 -cne $archive -or $manifest.archive -cne 'juce.zip' -or $manifest.license -cne 'AGPL-3.0-only' -or
    $manifest.physicalCallbackTelemetry -isnot [bool] -or $manifest.physicalCallbackTelemetry -ne $true -or
    $manifest.hostTransportSelection -isnot [bool] -or $manifest.hostTransportSelection -ne $false -or
    $null -ne $manifest.productionEnabled -or $null -ne $manifest.appliesAfter -or
    $dependency[0].revision -cne $revision -or $dependency[0].sourceDirectory -cne $directory -or
    $dependency[0].sha256 -cne $archive -or $dependency[0].archive -cne 'juce.zip' -or
    $dependency[0].license -cne 'AGPL-3.0-only') {
    throw 'Physical callback pinned dependency contract mismatch; no source changed'
}
if ($manifest.files.Count -ne $approved.Count -or
    @($manifest.files.path | Select-Object -Unique).Count -ne $approved.Count) {
    throw 'Physical callback target set mismatch; no source changed'
}
if ($manifest.vendorReadDependencies.Count -ne $readApproved.Count -or
    @($manifest.vendorReadDependencies.path | Select-Object -Unique).Count -ne $readApproved.Count -or $null -ne $manifest.sourceDependency -or $null -ne $manifest.sourceDependencies) {
    throw 'Physical callback unexpected vendor/app dependency contract; no source changed'
}
$relative='vendor/source/'+$directory
$source=[IO.Path]::GetFullPath((Join-Path $root $relative))
Assert-NoReparse $source
$readInputs=@()
foreach ($entry in $manifest.vendorReadDependencies) {
    $expected=@($readApproved | Where-Object path -CEQ $entry.path)
    if ($expected.Count -ne 1 -or $entry.mode -cne 'raw' -or $entry.sha256 -cne $expected[0].sha256) {
        throw 'Physical callback exact read dependency contract mismatch; no source changed'
    }
    $target=Join-Path $source $entry.path
    Assert-NoReparse $target
    if ((Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant() -cne $entry.sha256) {
        throw 'Physical callback native proof source mismatch; no source changed'
    }
    $readInputs+=@{path=$target; hash=$entry.sha256}
}
$inputPins=@($readInputs)
foreach ($path in @($manifestPath,'dependencies.lock.json','scripts/apply-physical-callback-patch.ps1')) {
    $target=Join-Path $root $path
    $inputPins+=@{path=$target; hash=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()}
}
$changes=@()
$pending=@()
$scratchParent=[IO.Path]::GetFullPath((Join-Path $root 'build/physical-callback-check/preflight'))
$scratchRelative='build/physical-callback-check/preflight/'+[Guid]::NewGuid().ToString('N')
$scratch=[IO.Path]::GetFullPath((Join-Path $root $scratchRelative))
Assert-NoReparse $scratch
$leaseParent=Join-Path $root 'build/physical-callback-check'
Assert-NoReparse $leaseParent
New-Item -ItemType Directory -Path $leaseParent -Force | Out-Null
$leasePath=Join-Path $leaseParent 'apply.lock'
Assert-NoReparse $leasePath
# This specific handle owns the lease; DeleteOnClose cannot delete a successor's lock.
$lease=[IO.FileStream]::new($leasePath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None,1,[IO.FileOptions]::DeleteOnClose)
Push-Location $root
try {
    # Validate every live target and payload before creating any mirror or writing source.
    foreach ($entry in $manifest.files) {
        $expected=@($approved | Where-Object path -CEQ $entry.path)
        if ($expected.Count -ne 1 -or
            $entry.patch -cne ('patches/juce-physical-callback/'+[IO.Path]::GetFileName($entry.path)+'.patch') -or
            $entry.originalSha256 -cne $expected[0].originalSha256 -or
            $entry.patchedSha256 -cne $expected[0].patchedSha256 -or
            $entry.patchSha256 -cne $expected[0].patchSha256) {
            throw 'Physical callback exact target identity/path mismatch; no source changed'
        }
        $target=[IO.Path]::GetFullPath((Join-Path $source $entry.path))
        Assert-NoReparse $target
        if (-not $target.StartsWith($source+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Physical callback target escapes pinned source'
        }
        $patch=Join-Path $root $entry.patch
        Assert-NoReparse $patch
        if ((Get-FileHash -LiteralPath $patch).Hash.ToLowerInvariant() -cne $entry.patchSha256) {
            throw 'Physical callback patch payload identity mismatch; no source changed'
        }
        $headers=@(Get-Content -LiteralPath $patch | Where-Object {$_ -match '^(---|\+\+\+) '})
        if ($headers.Count -ne 2 -or $headers[0] -cne ('--- a/'+$entry.path) -or
            $headers[1] -cne ('+++ b/'+$entry.path)) {
            throw 'Physical callback patch header mismatch; no source changed'
        }
        $stats=@(& git -c "safe.directory=$root" -c core.longpaths=true -c core.autocrlf=false -c core.eol=lf apply --numstat $patch)
        if ($LASTEXITCODE -ne 0 -or $stats.Count -ne 1 -or
            $stats[0] -cnotmatch ('^\d+\t\d+\t'+[regex]::Escape($entry.path)+'$')) {
            throw 'Physical callback patch changes unexpected or binary target'
        }
        $actual=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()
        if ($actual -cne $entry.originalSha256 -and $actual -cne $entry.patchedSha256) {
            throw 'Unrecognized physical callback source preserved; no source changed'
        }
        $bytes=[IO.File]::ReadAllBytes($target)
        $sha=[Security.Cryptography.SHA256]::Create()
        try {$bytesHash=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant()} finally {$sha.Dispose()}
        if ($bytesHash -cne $actual) {throw 'Physical callback source changed while reading; source preserved'}
        $changes+=@{entry=$entry; target=$target; patch=$patch; initial=$actual; bytes=$bytes; applied=($actual -ceq $entry.patchedSha256)}
        $inputPins+=@{path=$patch; hash=$entry.patchSha256}
        if ($actual -ceq $entry.originalSha256) {$pending+=$patch}
    }
    # Reverse already-applied inputs in a disposable mirror to prove originals,
    # then apply the physical callback patch there and check every exact raw CRLF post-image.
    foreach ($change in $changes) {
        $copy=Join-Path $scratch $change.entry.path
        New-Item -ItemType Directory -Path (Split-Path $copy -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $change.target -Destination $copy
        if ($change.applied) {
            & git -c "safe.directory=$root" -c core.longpaths=true -c core.autocrlf=false -c core.eol=lf apply --reverse --directory=$scratchRelative $change.patch
            if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant() -cne $change.entry.originalSha256) {
                throw 'Physical callback reverse preimage mismatch; no dependency source changed'
            }
        }
    }
    $patches=@($changes | ForEach-Object {$_.patch})
    $arguments=@('-c',"safe.directory=$root",'-c','core.longpaths=true','-c','core.autocrlf=false','-c','core.eol=lf',
                 'apply',"--directory=$scratchRelative")+$patches
    & git @arguments
    if ($LASTEXITCODE -ne 0) {throw 'Physical callback isolated combined preflight failed; no dependency source changed'}
    foreach ($change in $changes) {
        if ((Get-FileHash -LiteralPath (Join-Path $scratch $change.entry.path)).Hash.ToLowerInvariant() -cne $change.entry.patchedSha256) {
            throw 'Physical callback declared raw post-image mismatch; no dependency source changed'
        }
    }
    if (-not $VerifyOnly -and $pending.Count -gt 0) {
        foreach ($pin in $inputPins) {
            Assert-NoReparse $pin.path
            if ((Get-FileHash -LiteralPath $pin.path).Hash.ToLowerInvariant() -cne $pin.hash) {
                throw 'Physical callback contract input changed during preflight; source preserved'
            }
        }
        foreach ($change in $changes) {
            Assert-NoReparse $change.target
            if ((Get-FileHash -LiteralPath $change.target).Hash.ToLowerInvariant() -cne $change.initial) {
                throw 'Physical callback input changed during preflight; source preserved'
            }
        }
        $arguments=@('-c',"safe.directory=$root",'-c','core.longpaths=true','-c','core.autocrlf=false','-c','core.eol=lf',
                     'apply','--check',"--directory=$relative")+$pending
        & git @arguments
        if ($LASTEXITCODE -ne 0) {throw 'Physical callback final preflight failed; no dependency source changed'}
        try {
            $arguments=@('-c',"safe.directory=$root",'-c','core.longpaths=true','-c','core.autocrlf=false','-c','core.eol=lf',
                         'apply',"--directory=$relative")+$pending
            & git @arguments
            if ($LASTEXITCODE -ne 0) {throw 'Physical callback patch application failed'}
            foreach ($change in $changes) {
                if ((Get-FileHash -LiteralPath $change.target).Hash.ToLowerInvariant() -cne $change.entry.patchedSha256) {
                    throw 'Physical callback final raw identity mismatch'
                }
            }
        } catch {
            $failure=$_.Exception.Message
            $rollbackFailures=@()
            foreach ($change in $changes) {
                try {
                    Assert-NoReparse $change.target
                    $now=(Get-FileHash -LiteralPath $change.target).Hash.ToLowerInvariant()
                    if ($now -ceq $change.initial) {continue}
                    if (-not $change.applied -and $now -ceq $change.entry.patchedSha256) {
                        [IO.File]::WriteAllBytes($change.target,$change.bytes)
                        if ((Get-FileHash -LiteralPath $change.target).Hash.ToLowerInvariant() -cne $change.initial) {
                            throw 'restored identity mismatch'
                        }
                    } else {throw 'unrecognized concurrent bytes preserved'}
                } catch {$rollbackFailures+=($change.entry.path+': '+$_.Exception.Message)}
            }
            if ($rollbackFailures.Count -gt 0) {throw ($failure+'; rollback incomplete: '+($rollbackFailures -join '; '))}
            throw ($failure+'; all admitted dependency inputs restored')
        }
    }
    Write-Output 'Verified pinned physical callback telemetry; exact raw identities; Host policy unchanged.'
} finally {
    Pop-Location
    $lease.Dispose()
    if (-not $scratch.StartsWith($scratchParent+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Physical callback scratch cleanup escaped its task directory'
    }
    Assert-NoReparse $scratch
    if (Test-Path -LiteralPath $scratch) {Remove-Item -LiteralPath $scratch -Recurse -Force}
}
