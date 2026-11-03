# Re-enter legacy entrypoints under the same kernel lease as scripts/dev.py.
function Invoke-VibeRaEngineeringOwner([string]$Purpose,[string]$Script,[System.Collections.IDictionary]$Parameters) {
    $taskPython = Join-Path $PSScriptRoot 'dev.py'
    $taskArguments = @($taskPython, 'owned', $Purpose, '--', 'powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $Script)
    if($env:VIBERA_DEV_OWNER_TOKEN){
        & python -c "import sys;sys.path.insert(0,sys.argv[1]);from dev_support import EngineeringOwner;EngineeringOwner(sys.argv[2],'nested').__enter__()" $PSScriptRoot (Split-Path $PSScriptRoot -Parent) | Out-Host
        if($LASTEXITCODE -ne 0){throw 'Invalid engineering owner'}
        return $null
    }
    foreach($taskKey in $Parameters.Keys){
        $taskValue=$Parameters[$taskKey]
        if($taskValue -is [System.Management.Automation.SwitchParameter]){
            if($taskValue.IsPresent){$taskArguments += '-'+$taskKey}
        } else {$taskArguments += '-'+$taskKey; $taskArguments += @($taskValue)}
    }
    & python @taskArguments | Out-Host
    return [int]$LASTEXITCODE
}
