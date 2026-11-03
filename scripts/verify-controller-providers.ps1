param([ValidateSet('controllers','automation')][string]$Prefix='controllers',
      [string]$Instrument='C:/Program Files/Common Files/VST3/Arturia/Analog Lab V.vst3',
      [string]$Effect='C:/Program Files/Common Files/VST3/MeldaProduction/Modulation/MVibratoMB.vst3')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$app=Join-Path $root 'dist/VibeRa Studio/VibeRa Studio.exe'
$runner=Join-Path $root 'dist/VibeRa Studio/VibeRaTests.exe'
$worker=Join-Path $root 'dist/VibeRa Studio/VibeRa Plugin Worker.exe'
foreach($path in @($Instrument,$Effect,$app,$runner,$worker)) {
    if(-not (Test-Path -LiteralPath $path)) {throw "Required installed provider/build missing: $path"}
}
function Invoke-Owned([string]$Executable,[string[]]$Arguments,[string]$Purpose,[string]$Log) {
    # Retain the instance handle. This script never terminates by executable name.
    $quoted=@($Arguments | ForEach-Object {'"'+$_+'"'})
    $process=Start-Process -FilePath $Executable -ArgumentList $quoted -PassThru -WindowStyle Hidden -RedirectStandardOutput $Log -RedirectStandardError ($Log+'.error')
    $handle=$process.Handle
    $receipt=[ordered]@{pid=$process.Id;creation=$process.StartTime.ToUniversalTime().ToString('o');purpose=$Purpose;applicationSha256=(Get-FileHash -LiteralPath $app).Hash}
    $process.WaitForExit()
    $receipt.exitCode=$process.ExitCode
    $receipt | ConvertTo-Json | Set-Content -LiteralPath ($Log+'.process.json') -Encoding UTF8
    if($process.ExitCode -ne 0) {throw "$Purpose failed: $($process.ExitCode); $Log"}
    return $receipt
}
$provider=Join-Path $root "build/$Prefix-plugin-acceptance"
$proof=Invoke-Owned $runner @('--plugin-check',$provider,$Instrument,$Effect) "Installed instrument/effect $Prefix regression" (Join-Path $root "build/$Prefix-actual-plugin.log")
@{exitCode=$proof.exitCode;applicationSha256=(Get-FileHash -LiteralPath $app).Hash;runnerSha256=(Get-FileHash -LiteralPath $runner).Hash;workerSha256=(Get-FileHash -LiteralPath $worker).Hash} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root "build/$Prefix-provider-provenance.json") -Encoding UTF8
# Original generated fixture PCM, never FL Studio samples.
[xml]$sample=Get-Content -LiteralPath (Join-Path $root 'build/windows/gui_acceptance/midnight-session.vibra') -Raw
$sample.VIBERA.CHANNELS.CHANNEL[0].SetAttribute('source',(Join-Path $root 'build/windows/acceptance/44100-16bit.wav'))
$sampleFile=Join-Path $root "build/$Prefix-sample.vibra"
$sample.Save($sampleFile)
$openings=@()
foreach($kind in @('sample','vst3')) {
    $fixture=if($kind -eq 'sample') {$sampleFile} else {Join-Path $provider 'hosted-project.vibra'}
    $openings+=Invoke-Owned $app @('--instrument-check',$fixture,(Join-Path $root "build/$Prefix-opening-$kind")) "$Prefix checkpoint instrument opening $kind" (Join-Path $root "build/$Prefix-opening-$kind.log")
}
$openings | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root "build/$Prefix-opening-processes.json") -Encoding UTF8
$live=Invoke-Owned $app @('--audio-playlist-live-check',(Join-Path $root "build/$Prefix-live-plugin"),(Join-Path $provider 'mixed-audio-plugin.vibra')) "$Prefix checkpoint hosted Playlist playback" (Join-Path $root "build/$Prefix-live-plugin.log")
$live | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root "build/$Prefix-live-plugin-process.json") -Encoding UTF8
