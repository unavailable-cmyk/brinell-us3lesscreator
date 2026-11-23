param([string]$Directory=(Join-Path $PSScriptRoot '..\build\diagnostic-reports'), [string]$Prefix='census')
$ErrorActionPreference='Stop'
$culture=[Globalization.CultureInfo]::InvariantCulture
function N($s){[double]::Parse($s,$culture)}
$rows=@()
foreach($level in @('small','peaceful')){foreach($repeat in 1..2){foreach($variant in @('control','on')){
 $folder=Join-Path $Directory "$Prefix-$level-$variant-$repeat"
 $file=Join-Path $folder 'batching.csv'
 if(!(Test-Path $file)){continue}
 $lo=if($level-eq'small'){0.5}else{80};$hi=if($level-eq'small'){3}else{90}
 $a=@(Import-Csv $file|Where-Object{(N $_.level_time)-ge$lo-and(N $_.level_time)-lt$hi})
 if($a.Count-lt 2){continue}
 $times=@($a|ForEach-Object{N $_.frame_ms}|Sort-Object)
 $total=($times|Measure-Object -Sum).Sum
 $wall=((N $a[-1].wall_ms)-(N $a[0].wall_ms))/1000
  $middle=[int][math]::Floor($times.Count/2)
  $median=if($times.Count%2){$times[$middle]}else{($times[$middle-1]+$times[$middle])/2}
  $rows+=[pscustomobject]@{level=$level;variant=$variant;repeat=$repeat;level_begin=$lo;level_end=$hi;frames=$a.Count;mean_ms=$total/$a.Count;median_ms=$median;fps=1000*$a.Count/$total;p95_ms=$times[[math]::Ceiling($times.Count*.95)-1];p99_ms=$times[[math]::Ceiling($times.Count*.99)-1];progression=((N $a[-1].level_time)-(N $a[0].level_time))/$wall}
}}}
$rows|ConvertTo-Json -Depth 4|Set-Content (Join-Path $Directory "$Prefix-timing.json") -Encoding UTF8
$rows|Format-Table level,variant,repeat,frames,@{n='median_ms';e={'{0:F3}'-f$_.median_ms}},@{n='p95';e={'{0:F2}'-f$_.p95_ms}},@{n='p99';e={'{0:F2}'-f$_.p99_ms}},@{n='game/wall';e={'{0:F3}'-f$_.progression}}
