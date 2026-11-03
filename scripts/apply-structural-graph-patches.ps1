param([Alias('VerifyOnly')][switch]$ValidateOnly)
$ErrorActionPreference='Stop'
# Includes pinned strict/LOCAL-CLOSED and Summing property-reuse successors.
# Includes optional send block provider (default capability false).
# Source-selection and retained-history production opt-ins remain OFF.
$helper=Join-Path $PSScriptRoot 'guard-structural-graph.py'
if ($ValidateOnly) { & python $helper --verify-only } else { & python $helper }
if ($LASTEXITCODE -ne 0) {throw 'Strict structural graph dependency guard failed'}
