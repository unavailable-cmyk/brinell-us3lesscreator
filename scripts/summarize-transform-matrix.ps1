param([Parameter(Mandatory)][string]$Directory)
$ErrorActionPreference='Stop'
$culture=[Globalization.CultureInfo]::InvariantCulture
function Number($text){[double]::Parse($text,$culture)}
$rows=@()
foreach($mode in @('native-opengl','native-zink','batching','culling')){
 $frames=Import-Csv (Join-Path $Directory ($mode+'.csv'))
 foreach($range in @(@(73,80),@(80,90),@(90,100))){
  $sample=@($frames|Where-Object{(Number $_.level_time)-ge$range[0]-and(Number $_.level_time)-lt$range[1]})
  if($sample.Count-lt 2){continue}
  $times=@($sample|ForEach-Object{Number $_.frame_ms}|Sort-Object)
  $sum=($times|Measure-Object -Sum).Sum
  $wall=((Number $sample[-1].wall_ms)-(Number $sample[0].wall_ms))/1000
  $level=(Number $sample[-1].level_time)-(Number $sample[0].level_time)
  $ticks=(Number $sample[-1].physics_steps)-(Number $sample[0].physics_steps)
  $rows+=[pscustomobject]@{mode=$mode;section="$($range[0])..$($range[1])";frames=$sample.Count;fps=1000*$sample.Count/$sum;mean_ms=$sum/$sample.Count;p95_ms=$times[[Math]::Ceiling($times.Count*.95)-1];p99_ms=$times[[Math]::Ceiling($times.Count*.99)-1];max_ms=$times[-1];level_per_wall=$level/$wall;ticks_per_wall=$ticks/$wall}
 }
}
$rows|ConvertTo-Json -Depth 4|Set-Content (Join-Path $Directory 'frame-summary.json') -Encoding UTF8
$rows|Format-Table mode,section,frames,@{n='FPS';e={'{0:F1}'-f$_.fps}},@{n='p95 ms';e={'{0:F1}'-f$_.p95_ms}},@{n='p99 ms';e={'{0:F1}'-f$_.p99_ms}},@{n='game/wall';e={'{0:F3}'-f$_.level_per_wall}} -AutoSize
