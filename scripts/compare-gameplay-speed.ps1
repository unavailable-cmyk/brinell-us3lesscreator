param(
    [Parameter(Mandatory)][string]$ReferenceLog,
    [Parameter(Mandatory)][string]$CandidateLog,
    [Parameter(Mandatory)][string]$OutputPath
)
$ErrorActionPreference='Stop'
$culture=[Globalization.CultureInfo]::InvariantCulture
function Read-SpeedCapture([string]$path) {
    $points=@{}; $windows=@(); $build=$null; $completed=$false; $override=$false; $active=$false; $gameStarted=$false; $nativeBaseline=$false
    foreach($line in [IO.File]::ReadLines((Resolve-Path -LiteralPath $path).Path)) {
        if($line -match 'VULKANIZER_BUILD (.+)$'){$build=$Matches[1]}
        if($line -match 'VULKANIZER_SCHEDULER_GAMEPLAY_TEST starting'){$gameStarted=$true}
        if($line -match 'VULKANIZER_FIXED_INPUT|timing_override=true'){$override=$true}
        if($line -match 'VULKANIZER_SCHEDULER_GAMEPLAY_TEST result=PASS'){$completed=$true}
        if($line -match 'VULKANIZER_GAMEPLAY_TEST_MODE native_baseline=true'){$nativeBaseline=$true}
        if($gameStarted -and $line -match 'VULKANIZER_VISUAL_GPU_PARITY PASS'){$active=$true}
        if($line -match 'VULKANIZER_PLAYER_CHECKPOINT (.+)$'){
            $values=@{};foreach($item in [regex]::Matches($Matches[1],'(\w+)=([^ ]+)')){$values[$item.Groups[1].Value]=$item.Groups[2].Value}
            $points[$values.level_time]=$values
        }
        if($line -match 'VULKANIZER_GAMEPLAY_SPEED (.+)$'){
            $values=@{};foreach($item in [regex]::Matches($Matches[1],'(\w+)=([^ ]+)')){$values[$item.Groups[1].Value]=$item.Groups[2].Value}
            if($values.comparable_window -eq 'true' -and [double]::Parse($values.level_time,$culture) -ge 73 -and [double]::Parse($values.level_time,$culture) -le 92){$windows+=$values}
        }
    }
    return @{points=$points;windows=$windows;build=$build;completed=$completed;override=$override;gpu_active=$active;native_baseline=$nativeBaseline}
}
function Summarize-Speed($capture){
    $wall=0.0;$level=0.0;$ticks=0.0;$music=0.0;$frames=0
    foreach($w in $capture.windows){$wall+=[double]::Parse($w.wall_s,$culture);$level+=[double]::Parse($w.level_s,$culture);$ticks+=[double]::Parse($w.ticks,$culture);$music+=[double]::Parse($w.music0_delta_s,$culture);$frames+=[int]$w.frames}
    if($wall -le 0){return $null}
    return @{windows=$capture.windows.Count;wall_s=$wall;level_s=$level;level_per_wall=$level/$wall;ticks_per_wall=$ticks/$wall;fps=$frames/$wall;music0_per_wall=$music/$wall;audio_minus_level_s=$music-$level;full_speed=([Math]::Abs($level/$wall-1)-le 0.05);audio_clock_available=($music -gt 0)}
}
$a=Read-SpeedCapture $ReferenceLog;$b=Read-SpeedCapture $CandidateLog
$matched=0;$matchedHeavy=0;$diff=@();$fields=@('x','y','vx','vy','acceleration_or_speed_raw','speed_portal','speed_multiplier','warp','mode','upside_down','dead')
foreach($time in $b.points.Keys|Sort-Object){
    if(!$a.points.ContainsKey($time)){continue}
    $changed=@($fields|Where-Object{$a.points[$time][$_] -ne $b.points[$time][$_]})
    if(!$changed.Count){$matched++;if([double]::Parse($time,$culture)-ge 73){$matchedHeavy++}}else{$diff+=@{level_time=$time;fields=$changed;reference=$a.points[$time];candidate=$b.points[$time]}}
}
$referenceSpeed=Summarize-Speed $a;$candidateSpeed=Summarize-Speed $b
$valid=$a.native_baseline-and$a.completed-and$b.completed-and$a.build-and($a.build-eq$b.build)-and(!$a.override)-and(!$b.override)-and($matched-ge 24)-and($matchedHeavy-ge 4)-and(!$diff.Count)-and$referenceSpeed.full_speed-and$candidateSpeed.full_speed-and$b.gpu_active
$result=[ordered]@{
    valid_gameplay_speed_parity=[bool]$valid;reference_log=$ReferenceLog;candidate_log=$CandidateLog
    same_build=([bool]$a.build-and$a.build-eq$b.build);native_timing=(!$a.override-and!$b.override)
    native_reference=$a.native_baseline
    matching_player_checkpoints=$matched;mismatching_player_checkpoints=$diff.Count;first_mismatches=@($diff|Select-Object -First 2)
    matching_heavy_player_checkpoints=$matchedHeavy
    native_clock_rate_parity=([bool]$referenceSpeed.full_speed-and$candidateSpeed.full_speed-and!$a.override-and!$b.override)
    audio_sync_verified=($referenceSpeed.audio_clock_available-and$candidateSpeed.audio_clock_available-and[Math]::Abs($referenceSpeed.audio_minus_level_s)-lt 0.15-and[Math]::Abs($candidateSpeed.audio_minus_level_s)-lt 0.15)
    reference=$referenceSpeed;candidate=$candidateSpeed;real_gpu_stream_active=$b.gpu_active
    limitation='Checkpoints compare sampled native player state at identical level times. speed_ac is retained in logs but is not treated as a stable cross-process physical velocity. Raw acceleration field name is unverified; native velocity is also checked. Audio channel 0 requires a moving music clock. This is not universal level/mod compatibility.'
}
$result|ConvertTo-Json -Depth 7|Set-Content -LiteralPath $OutputPath -Encoding UTF8
[pscustomobject]@{valid_gameplay_speed_parity=[bool]$valid;matching=$matched;mismatching=$diff.Count;report=$OutputPath}|ConvertTo-Json -Compress
if(!$valid){exit 1}
