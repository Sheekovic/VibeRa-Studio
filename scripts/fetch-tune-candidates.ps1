# SPDX-License-Identifier: AGPL-3.0-or-later
# Isolated research dependencies. This does not change the production dependency lock.
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$candidates=@(
    @{directory='tune-upstream-a670068';url='https://github.com/Signalsmith-Audio/signalsmith-stretch.git';branch='main';revision='a670068d9aeb64913331d5cc29337b19a457a7df'},
    @{directory='tune-linear-de55e6a';url='https://github.com/Signalsmith-Audio/linear.git';branch='0.6.4';revision='de55e6a50ffcf6f8f43f649692d94691c7025151'},
    @{directory='tune-rubberband-1d95888';url='https://github.com/breakfastquay/rubberband.git';branch='v4.0.0';revision='1d95888bec3ae0a17c0c4af791810d5a63f6bc35'}
)
foreach($candidate in $candidates){
    $directory=Join-Path $root ('build/'+$candidate.directory)
    if(-not(Test-Path -LiteralPath $directory)){
        & git clone --no-checkout --depth 1 --branch $candidate.branch $candidate.url $directory
        if($LASTEXITCODE -ne 0){throw 'Candidate clone failed'}
        & git -C $directory fetch --depth 1 origin $candidate.revision
        if($LASTEXITCODE -ne 0){throw 'Pinned candidate fetch failed'}
        & git -C $directory checkout --detach $candidate.revision
        if($LASTEXITCODE -ne 0){throw 'Pinned candidate checkout failed'}
    }
    $revision=& git -c ("safe.directory="+$directory.Replace('\','/')) -C $directory rev-parse HEAD
    if($LASTEXITCODE -ne 0 -or $revision -ne $candidate.revision){
        throw ("Existing candidate revision differs; preserved without reset: "+$directory)
    }
    Write-Output ($candidate.directory+' '+$revision)
}
