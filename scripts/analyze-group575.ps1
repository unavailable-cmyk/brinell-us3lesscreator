param(
    [Parameter(Mandatory=$true)][string[]]$RunDirectories,
    [string]$ReportFile='build/diagnostic-reports/group575-comparison.json'
)
$ErrorActionPreference='Stop'
function Percentile($values,[double]$p){$values[[int][math]::Ceiling(($values.Count-1)*$p)]}
function ReadCounters([string]$line){
    $result=@{}
    foreach($m in [regex]::Matches($line,'(\w+)=([\w\.\-]+)')){$result[$m.Groups[1].Value]=$m.Groups[2].Value}
    return $result
}
$runs=@(foreach($directory in $RunDirectories){
    $rows=@(Import-Csv (Join-Path $directory 'batching.csv')|Where-Object{[double]$_.level_time-ge80-and[double]$_.level_time-lt90})
    if($rows.Count-lt30){throw "Insufficient heavy-window samples: $directory"}
    $plateauStart=[double]$rows[0].wall_ms;$previousLevel=[double]$rows[0].level_time
    foreach($row in $rows){
        if([double]$row.level_time-ne$previousLevel){$plateauStart=[double]$row.wall_ms;$previousLevel=[double]$row.level_time}
        elseif([double]$row.wall_ms-$plateauStart-gt250){throw "Invalid capture: simulation paused for over 250ms inside the heavy window: $directory"}
    }
    $times=@($rows|ForEach-Object{[double]$_.frame_ms}|Sort-Object)
    $log=[IO.File]::ReadAllLines((Join-Path (Resolve-Path $directory).Path 'batching.geode.log'))
    $points=@($log|Where-Object{$_-match 'VULKANIZER_GROUP575_WINDOW '}|ForEach-Object{ReadCounters $_})
    $delta=@{}
    if($points.Count){
        $start=$points|Where-Object{[double]$_.level_time-ge80}|Select-Object -First 1
        $end=$points|Where-Object{[double]$_.level_time-ge90}|Select-Object -First 1
        if(!$start-or!$end){throw "Incomplete counter window: $directory"}
        foreach($key in $start.Keys){if($key-notin@('step','level_time')){$delta[$key]=[long]$end[$key]-[long]$start[$key]}}
        $delta.level_start=[double]$start.level_time;$delta.level_end=[double]$end.level_time
    }
    [pscustomobject]@{
        run=(Split-Path $directory -Leaf);frames=$times.Count;fps=1000/(($times|Measure-Object -Average).Average)
        median_ms=(Percentile $times .5);p95_ms=(Percentile $times .95);p99_ms=(Percentile $times .99)
        counters=$delta
        scheduler_parity_pass=[bool]($log-match 'VULKANIZER_SCHEDULER_PARITY PASS')
        group575_reference_batches=@($log-match 'VULKANIZER_GROUP575_PARITY PASS').Count
        gameplay_test_pass=[bool]($log-match 'VULKANIZER_SCHEDULER_GAMEPLAY_TEST result=PASS')
        executor_errors=@($log|Where-Object{$_-match 'GPU stream disabled|PARITY FAIL|VK_ERROR|timeout'}).Count
    }
})
$reference=@{}
foreach($line in [IO.File]::ReadLines((Join-Path (Resolve-Path $RunDirectories[0]).Path 'batching.geode.log'))){
    if($line-match 'PLAYER_CHECKPOINT step=(\d+) (.*)'){$reference[$matches[1]]=$matches[2]}
}
$playerChecks=@(foreach($directory in $RunDirectories|Select-Object -Skip 1){
    $compared=0;$differing=0;$rawDiffering=0;$timeUnmatched=0
    foreach($line in [IO.File]::ReadLines((Join-Path (Resolve-Path $directory).Path 'batching.geode.log'))){
        if($line-match 'PLAYER_CHECKPOINT step=(\d+) (.*)'){
            $step=$matches[1];$state=$matches[2]
            if(!$reference.ContainsKey($step)){continue}
            $referenceTime=([regex]::Match($reference[$step],'level_time=([^ ]+)')).Groups[1].Value
            $candidateTime=([regex]::Match($state,'level_time=([^ ]+)')).Groups[1].Value
            if($referenceTime-ne$candidateTime){++$timeUnmatched;continue}
            ++$compared
            if($reference[$step]-ne$state){++$rawDiffering}
            # SDK field semantics are not established; it differs in controls too.
            if(($reference[$step]-replace ' speed_ac=[^ ]+','')-ne($state-replace ' speed_ac=[^ ]+','')){++$differing}
        }
    }
    [pscustomobject]@{run=(Split-Path $directory -Leaf);compared=$compared;time_unmatched=$timeUnmatched;verified_field_differences=$differing;including_unverified_speed_ac_differences=$rawDiffering}
})
$snapshotChecks=@(foreach($directory in $RunDirectories|Select-Object -Skip 1){
    $left=Join-Path $RunDirectories[0] 'transform-optimized-9600.csv'
    $right=Join-Path $directory 'transform-optimized-9600.csv'
    if(!(Test-Path $left)-or!(Test-Path $right)){continue}
    $a=@(Import-Csv $left);$b=@(Import-Csv $right)
    if($a.Count-ne$b.Count){throw 'Snapshot sample counts differ'}
    $diff=@{};$maxX=0.;$maxY=0.
    for($i=0;$i-lt$a.Count;$i++){
        if($a[$i].index-ne$b[$i].index-or$a[$i].id-ne$b[$i].id){throw 'Snapshot object identities differ'}
        foreach($key in @('x_bits','y_bits','rx_offset_bits','ry_offset_bits','node_rx_bits','node_ry_bits','history_x_bits','history_y_bits','stamp')){
            if($a[$i].$key-ne$b[$i].$key){$diff[$key]++}
        }
        foreach($key in @('x_bits','y_bits')){
            $x=[BitConverter]::ToDouble([BitConverter]::GetBytes([Convert]::ToUInt64($a[$i].$key,16)),0)
            $y=[BitConverter]::ToDouble([BitConverter]::GetBytes([Convert]::ToUInt64($b[$i].$key,16)),0)
            if($key-eq'x_bits'){$maxX=[Math]::Max($maxX,[Math]::Abs($x-$y))}else{$maxY=[Math]::Max($maxY,[Math]::Abs($x-$y))}
        }
    }
    [pscustomobject]@{run=(Split-Path $directory -Leaf);samples=$a.Count;differing_fields=$diff;max_abs_x_difference=$maxX;max_abs_y_difference=$maxY;interpretation='Frame-partition-dependent snapshot, not an identical-input arithmetic parity test; compare native-control repeat variation'}
})
$report=[pscustomobject]@{frame_window='native level_time [80,90) seconds';counter_window='first physics checkpoint at or after 80 and 90 seconds';percentile='higher order statistic: zero-based (n-1)*p, rounded up';runs=$runs;player_checks=$playerChecks;snapshot_checks=$snapshotChecks}
$report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $ReportFile -Encoding UTF8
$runs|Select-Object run,frames,fps,median_ms,p95_ms,p99_ms,executor_errors|Format-Table -AutoSize
