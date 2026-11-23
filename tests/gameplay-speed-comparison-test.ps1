param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
[IO.Directory]::CreateDirectory($OutputDirectory)|Out-Null
$reference=Join-Path $OutputDirectory 'speed-parser-reference.log'
$candidate=Join-Path $OutputDirectory 'speed-parser-candidate.log'
$report=Join-Path $OutputDirectory 'speed-parser-report.json'
$compare=Join-Path $PSScriptRoot '..\scripts\compare-gameplay-speed.ps1'
$lines=@('VULKANIZER_BUILD test-build','VULKANIZER_SCHEDULER_GAMEPLAY_TEST starting','VULKANIZER_VISUAL_GPU_PARITY PASS')
60..91|ForEach-Object{$lines+="VULKANIZER_PLAYER_CHECKPOINT step=$_ level_time=$_ x=12 y=15 vx=5 vy=0 acceleration_or_speed_raw=1 speed_portal=1 speed_multiplier=1 speed_ac=1 warp=1 mode=000000 upside_down=false dead=false physics_timing=native"}
$lines+='VULKANIZER_GAMEPLAY_SPEED level_time=80 wall_s=2 level_s=2 frames=60 ticks=480 music0_delta_s=2 comparable_window=true timing_override=false'
$lines+='VULKANIZER_SCHEDULER_GAMEPLAY_TEST result=PASS'
$lines+='VULKANIZER_GAMEPLAY_TEST_MODE native_baseline=true physics_steps=480 timing_override=false'
$lines|Set-Content -LiteralPath $reference -Encoding UTF8
$lines|Set-Content -LiteralPath $candidate -Encoding UTF8
& powershell.exe -NoProfile -File $compare -ReferenceLog $reference -CandidateLog $candidate -OutputPath $report
if($LASTEXITCODE-ne 0){throw 'Equivalent native speed was rejected'}
($lines-replace 'level_s=2 frames=60 ticks=480','level_s=1 frames=60 ticks=240')|Set-Content -LiteralPath $candidate -Encoding UTF8
& powershell.exe -NoProfile -File $compare -ReferenceLog $reference -CandidateLog $candidate -OutputPath $report
if($LASTEXITCODE-eq 0){throw 'Half-speed 30 FPS was incorrectly accepted'}
($lines-replace 'vx=5','vx=4')|Set-Content -LiteralPath $candidate -Encoding UTF8
& powershell.exe -NoProfile -File $compare -ReferenceLog $reference -CandidateLog $candidate -OutputPath $report
if($LASTEXITCODE-eq 0){throw 'Changed player velocity was incorrectly accepted'}
($lines-replace 'timing_override=false','timing_override=true')|Set-Content -LiteralPath $candidate -Encoding UTF8
& powershell.exe -NoProfile -File $compare -ReferenceLog $reference -CandidateLog $candidate -OutputPath $report
if($LASTEXITCODE-eq 0){throw 'Timing override was incorrectly accepted'}
'PASS: full-speed native state accepted; half-speed, velocity changes and timing overrides rejected'
