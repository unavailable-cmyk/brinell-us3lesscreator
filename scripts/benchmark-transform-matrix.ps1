param([string]$OutputDirectory=(Join-Path $PSScriptRoot '..\build\diagnostic-reports\throughput-matrix'), [string]$Modes='native-opengl,native-zink,batching,culling', [switch]$TransformDiagnostics, [switch]$Precompile, [switch]$Group575, [switch]$CpuOnly, [switch]$Resident, [switch]$ResidentFixture, [string]$LevelName='', [ValidateRange(5,90)][int]$DurationSeconds=45)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
[IO.Directory]::CreateDirectory($OutputDirectory)|Out-Null
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
$gameRoot='C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash'
$settings=Join-Path $env:LOCALAPPDATA 'GeometryDash\geode\mods\us3lesscreator.vulkanizer\settings.json'
if(Get-Process GeometryDash -ErrorAction SilentlyContinue){throw 'Close GD before the benchmark'}
$originalSettings=[IO.File]::ReadAllBytes($settings)
$env:VULKANIZER_VISUAL_CPU_ONLY=$(if($CpuOnly){'1'}else{$null})
$env:VULKANIZER_RESIDENT_FIXTURE=$(if($ResidentFixture){'1'}else{$null})
$env:VULKANIZER_RESIDENT_VALIDATE=$(if($Resident){'1'}else{$null})
try {
 foreach($mode in $Modes.Split(',')){
    $config=Get-Content -LiteralPath $settings -Raw|ConvertFrom-Json
    $config | Add-Member -NotePropertyName 'transform-cost-diagnostics' -NotePropertyValue ([bool]$TransformDiagnostics) -Force
    $config | Add-Member -NotePropertyName 'experimental-level-precompile' -NotePropertyValue ([bool]$Precompile) -Force
    $config | Add-Member -NotePropertyName 'experimental-group575-movement' -NotePropertyValue ([bool]$Group575) -Force
    $config | Add-Member -NotePropertyName 'experimental-resident-rendering' -NotePropertyValue ([bool]$Resident) -Force
    $config | Add-Member -NotePropertyName 'resident-decoration-group' -NotePropertyValue 0 -Force
    $config.'experimental-gpu-transform-all'=$true
    $config.'experimental-transform-culling'=($mode-eq'culling')
    $config.'frame-diagnostics'=$false
    $config|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $settings -Encoding UTF8
    $env:VULKANIZER_ROTATION_SELFTEST='1';$env:VULKANIZER_SCHEDULER_SELFTEST='1';$env:VULKANIZER_SCHEDULER_GAMEPLAY_TEST='1'
    $env:VULKANIZER_SCHEDULER_TEST_SECONDS=[string]$DurationSeconds;$env:VULKANIZER_GAMEPLAY_SPEED_AUDIT='1';$env:VULKANIZER_TRANSFORM_CHECKPOINTS='1';$env:VULKANIZER_SELFTEST_EXIT='1'
    [Environment]::SetEnvironmentVariable('VULKANIZER_TEST_LEVEL_NAME',$(if($LevelName){$LevelName}else{$null}),'Process')
    [Environment]::SetEnvironmentVariable('VULKANIZER_TRANSFORM_CULLING',$null,'Process')
    [Environment]::SetEnvironmentVariable('VULKANIZER_TRANSFORM_BATCHING_BASELINE',$null,'Process')
    [Environment]::SetEnvironmentVariable('VULKANIZER_SCHEDULER_FIXED_INPUT',$null,'Process')
    [Environment]::SetEnvironmentVariable('VULKANIZER_TRANSFORM_VANILLA_BASELINE',$(if($mode.StartsWith('native')){'1'}else{$null}),'Process')
    $env:VULKANIZER_BENCHMARK_FRAME_FILE=Join-Path $OutputDirectory ($mode+'.csv')
    $backend=if($mode-eq'native-opengl'){'OpenGL'}else{'Zink'}
    $args=@('-NoProfile','-File',('"'+(Join-Path $PSScriptRoot 'run-gd.ps1')+'"'),'-Backend',$backend,'-MesaDirectory',('"'+(Join-Path $root 'dist\zink-clean')+'"'))
    $process=Start-Process powershell.exe -WindowStyle Hidden -ArgumentList $args -WorkingDirectory $root -RedirectStandardOutput (Join-Path $OutputDirectory ($mode+'.stdout.log')) -RedirectStandardError (Join-Path $OutputDirectory ($mode+'.stderr.log')) -PassThru
    $deadline=[DateTime]::UtcNow.AddSeconds(90)
    while(!$process.WaitForExit(1000)){if([DateTime]::UtcNow-gt$deadline){throw "Benchmark $mode exceeded the deadline; stop and inspect the owned game process"}}
    $log=Get-ChildItem (Join-Path $gameRoot 'geode\logs') -Filter '*.log'|Sort-Object LastWriteTime -Descending|Select-Object -First 1
    Copy-Item -LiteralPath $log.FullName -Destination (Join-Path $OutputDirectory ($mode+'.geode.log'))
    if($ResidentFixture -and !(Select-String -LiteralPath $log.FullName -SimpleMatch 'VULKANIZER_RESIDENT_DRAW ')){throw 'Resident fixture did not reach actual atlas drawing; no resident success claimed'}
    if($ResidentFixture -and !(Select-String -LiteralPath $log.FullName -SimpleMatch 'VULKANIZER_RESIDENT_REFERENCE PASS')){throw 'Resident GPU state reference check did not pass'}
    if(!(Select-String -LiteralPath $log.FullName -SimpleMatch 'VULKANIZER_SCHEDULER_GAMEPLAY_TEST result=PASS')){throw "Benchmark $mode did not complete successfully"}
    if($Precompile -and !(Select-String -LiteralPath $log.FullName -SimpleMatch 'VULKANIZER_LEVEL_PRECOMPILE result=ready')){throw "Benchmark $mode fell back before level precompilation; no cache activation was verified"}
    if($Precompile -and !$mode.StartsWith('native')) {
        $gameStarted=$false; $liveStream=$false
        foreach($line in [IO.File]::ReadLines($log.FullName)) {
            if($line -match 'VULKANIZER_SCHEDULER_GAMEPLAY_TEST starting'){$gameStarted=$true}
            if($gameStarted -and $line -match 'VULKANIZER_VISUAL_GPU_PARITY PASS'){$liveStream=$true}
        }
        if(!$liveStream){throw "Benchmark $mode did not activate the precompiled visual stream during gameplay"}
    }
    Write-Output "Completed $mode"
 }
} finally {
 if(!(Get-Process GeometryDash -ErrorAction SilentlyContinue)){[IO.File]::WriteAllBytes($settings,$originalSettings)}
 else { [IO.File]::WriteAllBytes((Join-Path $OutputDirectory 'original-settings.json'),$originalSettings); Write-Warning 'GD remains running; original settings saved for restoration after exit' }
}
