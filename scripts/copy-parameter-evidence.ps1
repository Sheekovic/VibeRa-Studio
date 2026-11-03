param([Parameter(Mandatory=$true)][string]$Source,
      [Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference='Stop'
$workspaceRoot=Split-Path $PSScriptRoot -Parent
$buildPrefix=[IO.Path]::GetFullPath((Join-Path $workspaceRoot 'build'))+[IO.Path]::DirectorySeparatorChar
$workspacePrefix=[IO.Path]::GetFullPath($workspaceRoot)+[IO.Path]::DirectorySeparatorChar
$sourcePath=(Resolve-Path -LiteralPath $Source).Path
$destinationPath=[IO.Path]::GetFullPath($Destination)
if (-not (Get-Item -LiteralPath $sourcePath).PSIsContainer -or
    -not $sourcePath.StartsWith($buildPrefix,[StringComparison]::OrdinalIgnoreCase) -or
    -not $destinationPath.StartsWith($workspacePrefix,[StringComparison]::OrdinalIgnoreCase) -or
    $destinationPath.StartsWith($sourcePath+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or
    $destinationPath.Equals($sourcePath,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Evidence source must be a project build directory; destination must be a separate project directory.'
}
if (Test-Path -LiteralPath $destinationPath) {
    throw 'Preserve the prior incomplete parameter evidence assembly before copying again.'
}
New-Item -ItemType Directory -Path $destinationPath | Out-Null
$copied=@(); $excluded=@()
foreach ($file in Get-ChildItem -LiteralPath $sourcePath -Recurse -File | Sort-Object FullName) {
    $relative=$file.FullName.Substring($sourcePath.Length+1)
    # Provider projects and XML/native state stay local. These allowlisted outputs
    # are metrics, process receipts and scoped captures produced by our verifiers.
    if ($file.Extension -notin @('.json','.txt','.log','.error','.png')) {
        $excluded+=$relative
        continue
    }
    $destinationFile=Join-Path $destinationPath $relative
    New-Item -ItemType Directory -Path (Split-Path $destinationFile -Parent) -Force | Out-Null
    $hash=(Get-FileHash -LiteralPath $file.FullName).Hash
    Copy-Item -LiteralPath $file.FullName -Destination $destinationFile
    if ((Get-FileHash -LiteralPath $destinationFile).Hash -ne $hash) {throw 'Evidence changed while copying'}
    $copied+=@{path=$relative;sha256=$hash}
}
@{scope='Verification metrics/receipts/scoped captures only; provider projects and native state excluded';
  copied=$copied;excluded=$excluded} | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $destinationPath 'copy-manifest.json') -Encoding UTF8
Write-Output "Copied $($copied.Count) verification files; excluded $($excluded.Count) provider/data files."
