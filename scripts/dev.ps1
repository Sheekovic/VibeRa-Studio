param([switch]$Full,[switch]$Plan,[switch]$NoBuild,[string[]]$AdditionalTest=@(),
      [ValidateSet('Release','Debug')][string]$Configuration='Release')
$taskArguments=@((Join-Path $PSScriptRoot 'dev.py'),'--configuration',$Configuration)
if($Full){$taskArguments+='--full'}
if($Plan){$taskArguments+='--plan'}
if($NoBuild){$taskArguments+='--no-build'}
foreach($taskTest in $AdditionalTest){$taskArguments+=@('--additional-test',$taskTest)}
& python @taskArguments
exit $LASTEXITCODE
