param([Parameter(Mandatory=$true)][string[]]$RunDirectories,[string]$ReportFile='build/diagnostic-reports/stream-comparison.json')
$ErrorActionPreference='Stop'
& "$PSScriptRoot/analyze-group575.ps1" -RunDirectories $RunDirectories -ReportFile $ReportFile
$report=Get-Content $ReportFile -Raw|ConvertFrom-Json
for($i=0;$i-lt$RunDirectories.Count;$i++){
 $log=Get-Content (Join-Path $RunDirectories[$i] 'batching.geode.log')
 $backend=if($log-match 'VISUAL_EVALUATOR backend=CPU'){'CPU'}else{'GPU'}
 $progress=@($log|Where-Object{$_-match 'GAMEPLAY_SPEED level_time=8\d\.' -and $_-match 'comparable_window=true'}|ForEach-Object{if($_-match 'level_per_wall=([\d.]+)'){[double]$matches[1]}})
 $costs=@($log|Where-Object{$_-match 'EVALUATOR_COST step=(8160|10560) '}|ForEach-Object{$fields=@{};foreach($m in [regex]::Matches($_,'(\w+)=([\d.]+)')){$fields[$m.Groups[1].Value]=[double]$m.Groups[2].Value};$fields})
 $delta=@{}
 if($costs.Count-eq2){foreach($k in $costs[0].Keys){if($k-ne'step'){$delta[$k]=$costs[1][$k]-$costs[0][$k]}}}
 $report.runs[$i]|Add-Member -NotePropertyName evaluator -NotePropertyValue $backend
 $report.runs[$i]|Add-Member -NotePropertyName progression_level_per_wall_mean -NotePropertyValue (($progress|Measure-Object -Average).Average)
 $report.runs[$i]|Add-Member -NotePropertyName diagnostic_cost_deltas_ms -NotePropertyValue $delta
}
$report|ConvertTo-Json -Depth 10|Set-Content $ReportFile -Encoding UTF8
