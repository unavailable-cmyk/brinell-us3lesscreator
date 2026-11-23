param(
    [ValidateSet('baseline','shared')][string]$Mode='shared',
    [string]$LevelName,
    [ValidateRange(3,90)][int]$DurationSeconds=8,
    [string]$OutputDirectory='build/diagnostic-reports/v2-live',
    [ValidateSet('OpenGL','Zink')][string]$Backend='OpenGL',
    [switch]$ValidateReference,
    [switch]$Capture,
    [switch]$Census,
    [switch]$StructuralFixture,
    [switch]$StaticTransforms,
    [switch]$ColorShakeFixture,
    [switch]$LocalColorShake,
    [switch]$ToggleFixture,
    [switch]$PersistVisibility,
    [switch]$LateDependencies,
    [switch]$Headless,
    [switch]$HeadlessFixture,
    [switch]$HeadlessMaterial,
    [switch]$HeadlessAlpha,
    [switch]$SharedHistory,
    [double]$CaptureTime=0,
    [switch]$XLockFixture,
    [switch]$ResolvedXLock,
    [switch]$AggressiveOwnership,
    [switch]$GroupRotation,
    [switch]$NativeCallerAudit,
    [switch]$SpikeAudit,
    [switch]$TemporalReplay,
    [switch]$TemporalControl,
    [switch]$RevocationAudit,
    [switch]$PositionSetterAudit,
    [ValidateSet('0','815','575','848')][int]$SetterShadowGroup=0,
    [switch]$SetterShadowNative,
    [switch]$SetterShadowAligned,
    [switch]$HyperAggressive,
    [switch]$HyperNegativeControl,
    [switch]$PerformanceOnly,
    [switch]$FixedProfile,
    [switch]$NativeVisibilityProfile,
    [switch]$VisibilityOwnershipShadow,
    [switch]$MirrorWorklistAudit,
    [switch]$MirrorWorklistNegativeControl,
    [switch]$VisibilityCensus,
    [switch]$VisibilityFilter,
    [switch]$VisibilityFilterAudit,
    [switch]$VisibilityFilterNegativeControl,
    [switch]$PackedVisibilityFilter,
    [switch]$MirrorPrefetch,
    [switch]$AllowNoEliminated,
    [string]$PackagePath
)
$ErrorActionPreference='Stop'
if ($VisibilityFilterNegativeControl -and (!$VisibilityFilter -or !$VisibilityFilterAudit)) { throw 'VisibilityFilterNegativeControl requires VisibilityFilter and VisibilityFilterAudit' }
if ($PackedVisibilityFilter -and !$VisibilityFilter -and !$VisibilityFilterAudit) { throw 'PackedVisibilityFilter requires VisibilityFilter or VisibilityFilterAudit' }
if ($MirrorPrefetch -and !$HyperAggressive) { throw 'MirrorPrefetch requires HyperAggressive' }
if ($VisibilityFilter -or $VisibilityFilterAudit) {
    if (!$HyperAggressive -or !$LevelName) { throw 'VisibilityFilter requires HyperAggressive and a named level' }
    if ($VisibilityCensus -or $MirrorWorklistAudit -or $MirrorWorklistNegativeControl -or $ValidateReference -or $Capture -or $CaptureTime -gt 0 -or $Census -or $NativeCallerAudit -or $RevocationAudit -or $PositionSetterAudit -or $SetterShadowGroup -or $SetterShadowAligned -or $HyperNegativeControl -or $StructuralFixture -or $ColorShakeFixture -or $ToggleFixture -or $HeadlessFixture -or $XLockFixture) { throw 'Run visibility filtering separately from other diagnostics' }
    if ($VisibilityFilterAudit -and ($PerformanceOnly -or $FixedProfile)) { throw 'VisibilityFilterAudit controls its own fixed clock' }
    if (!$VisibilityFilterAudit -and !$PerformanceOnly -and !$FixedProfile) { throw 'Select PerformanceOnly, FixedProfile or VisibilityFilterAudit with VisibilityFilter' }
}
if ($VisibilityCensus) {
    if (!$HyperAggressive -or !$LevelName -or $PerformanceOnly -or $FixedProfile -or $MirrorWorklistAudit) { throw 'VisibilityCensus requires HyperAggressive and a named level; run separately from other diagnostics' }
    if ($ValidateReference -or $Capture -or $CaptureTime -gt 0 -or $Census -or $NativeCallerAudit -or $RevocationAudit -or $PositionSetterAudit -or $SetterShadowGroup -or $SetterShadowAligned -or $HyperNegativeControl -or $StructuralFixture -or $ColorShakeFixture -or $ToggleFixture -or $HeadlessFixture -or $XLockFixture) { throw 'VisibilityCensus isolates the visibility measurement from other diagnostics' }
}
if ($MirrorWorklistNegativeControl -and !$MirrorWorklistAudit) { throw 'MirrorWorklistNegativeControl requires MirrorWorklistAudit' }
if ($MirrorWorklistAudit) {
    if (!$HyperAggressive -or !$LevelName -or $PerformanceOnly -or $FixedProfile) { throw 'MirrorWorklistAudit requires HyperAggressive and a named level; run separately from performance modes' }
    if ($ValidateReference -or $Capture -or $CaptureTime -gt 0 -or $Census -or $NativeCallerAudit -or $RevocationAudit -or $PositionSetterAudit -or $SetterShadowGroup -or $SetterShadowAligned -or $HyperNegativeControl -or $StructuralFixture -or $ColorShakeFixture -or $ToggleFixture -or $HeadlessFixture -or $XLockFixture) { throw 'MirrorWorklistAudit isolates the old-scan oracle from other diagnostics' }
}
if ($FixedProfile) {
    if (!$HyperAggressive -or !$LevelName -or $PerformanceOnly) { throw 'FixedProfile requires HyperAggressive and a named level; use PerformanceOnly separately' }
    if ($ValidateReference -or $Capture -or $CaptureTime -gt 0 -or $Census -or $NativeCallerAudit -or $RevocationAudit -or $PositionSetterAudit -or $SetterShadowGroup -or $SetterShadowAligned -or $HyperNegativeControl -or $StructuralFixture -or $ColorShakeFixture -or $ToggleFixture -or $HeadlessFixture -or $XLockFixture) { throw 'FixedProfile excludes correctness audits and fixtures' }
}
if ($NativeVisibilityProfile) {
    if (!$FixedProfile -or !$HyperAggressive -or !$LevelName) { throw 'NativeVisibilityProfile requires FixedProfile, HyperAggressive and a named level' }
    if ($VisibilityFilter -or $VisibilityFilterAudit -or $PackedVisibilityFilter -or $MirrorPrefetch) { throw 'NativeVisibilityProfile requires the eager visibility path without filters or prefetch' }
}
if ($VisibilityOwnershipShadow) {
    if (!$FixedProfile -or !$HyperAggressive -or !$LevelName) { throw 'VisibilityOwnershipShadow requires FixedProfile, HyperAggressive and a named level' }
    if ($NativeVisibilityProfile -or $VisibilityFilter -or $VisibilityFilterAudit -or $PackedVisibilityFilter -or $MirrorPrefetch -or $VisibilityCensus -or $MirrorWorklistAudit) {
        throw 'VisibilityOwnershipShadow uses the eager path without native visibility profiling, filters, prefetch or other visibility audits'
    }
}
if ($PerformanceOnly -and ($ValidateReference -or $Capture -or $CaptureTime -gt 0 -or $Census -or $NativeCallerAudit -or $SpikeAudit -or $RevocationAudit -or $PositionSetterAudit -or $SetterShadowGroup -or $SetterShadowAligned -or $HyperNegativeControl -or $StructuralFixture -or $ColorShakeFixture -or $ToggleFixture -or $HeadlessFixture -or $XLockFixture)) { throw 'PerformanceOnly cannot be combined with validation, capture, audit or fixture options' }
if ($SetterShadowNative -and !$SetterShadowGroup) { throw 'Select a setter shadow group for the native reference' }
if ($SetterShadowAligned -and (!$SetterShadowGroup -or !$LevelName)) { throw 'Aligned diagnostics require a selected group and named level' }
if ($HyperNegativeControl -and !$HyperAggressive) { throw 'HyperNegativeControl requires HyperAggressive' }
if ($HyperAggressive) {
    if ($Mode -ne 'shared' -or $TemporalControl) { throw 'HyperAggressive requires shared temporal replay' }
    if (!$PerformanceOnly -and !$FixedProfile -and !$MirrorWorklistAudit -and !$VisibilityCensus -and !$VisibilityFilterAudit) {
        $ValidateReference = $true
        $PositionSetterAudit = $true
    }
    $TemporalReplay = $true
}
if ($SetterShadowGroup) {
    if ($TemporalControl -or $Mode -ne 'shared') { throw 'Setter shadow requires temporal replay in shared mode' }
    $PositionSetterAudit = $true
    $ValidateReference = $true
}
if ($TemporalControl) { $TemporalReplay = $true }
if ($PositionSetterAudit) { $RevocationAudit = $true }
if ($RevocationAudit) { $TemporalReplay = $true }
if ($TemporalReplay) {
    if ($Mode -ne 'shared') { throw 'Temporal replay requires shared mode' }
    $GroupRotation = $true
    if (!$PerformanceOnly) { $SpikeAudit = $true }
}
if ($GroupRotation) { $AggressiveOwnership = $true }
$root=Split-Path $PSScriptRoot -Parent
if (![IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory=Join-Path $root $OutputDirectory }
[IO.Directory]::CreateDirectory($OutputDirectory)|Out-Null
$gameRoot='C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash'
$installed=Join-Path $gameRoot 'geode/mods/us3lesscreator.vulkanizer.geode'
if (Get-Process GeometryDash -ErrorAction SilentlyContinue) { throw 'A Geometry Dash session is already open' }
$backup=Join-Path $OutputDirectory 'original-installed.geode'
Copy-Item -LiteralPath $installed -Destination $backup -Force
$settings=Join-Path $env:LOCALAPPDATA 'GeometryDash/geode/mods/us3lesscreator.vulkanizer/settings.json'
$originalSettings=[IO.File]::ReadAllBytes($settings)
$keys=@('VULKANIZER_V2','VULKANIZER_V2_BASELINE','VULKANIZER_V2_TEST','VULKANIZER_V2_VALIDATE','VULKANIZER_V2_CENSUS','VULKANIZER_V2_STRUCTURAL_FIXTURE','VULKANIZER_V2_STATIC_TRANSFORMS','VULKANIZER_V2_COLOR_SHAKE_FIXTURE','VULKANIZER_V2_LOCAL_COLOR_SHAKE','VULKANIZER_V2_TOGGLE_FIXTURE','VULKANIZER_V2_PERSIST_VISIBILITY','VULKANIZER_V2_LATE_DEPENDENCIES','VULKANIZER_V2_HEADLESS','VULKANIZER_V2_HEADLESS_FIXTURE','VULKANIZER_V2_HEADLESS_MATERIAL','VULKANIZER_V2_CAPTURE_TIME','VULKANIZER_V2_VERIFIED_STATIC','VULKANIZER_V2_XLOCK_FIXTURE','VULKANIZER_V2_RESOLVED_XLOCK','VULKANIZER_V2_TEST_SECONDS','VULKANIZER_V2_REPORT','VULKANIZER_V2_CAPTURE','VULKANIZER_TEST_LEVEL_NAME','VULKANIZER_ROTATION_SELFTEST','VULKANIZER_SCHEDULER_GAMEPLAY_TEST','VULKANIZER_RESIDENT_FIXTURE')
$previous=@{}
$keys+=@('VULKANIZER_V2_HEADLESS_ALPHA','VULKANIZER_V2_SHARED_HISTORY')
$keys+=@('VULKANIZER_V2_NATIVE_CALLERS')
$keys+=@('VULKANIZER_V2_SPIKE_AUDIT')
$keys+=@('VULKANIZER_V2_TEMPORAL_REPLAY','VULKANIZER_V2_TEMPORAL_CONTROL')
$keys+=@('VULKANIZER_V2_REVOCATION_AUDIT')
$keys+=@('VULKANIZER_V2_SETTER_AUDIT')
$keys+=@('VULKANIZER_V2_SETTER_SHADOW_GROUP','VULKANIZER_V2_SETTER_SHADOW_NATIVE')
$keys+=@('VULKANIZER_V2_SETTER_SHADOW_ALIGNED')
$keys+=@('VULKANIZER_V2_HYPER_AGGRESSIVE','VULKANIZER_V2_HYPER_NEGATIVE')
$keys+=@('VULKANIZER_V2_PERFORMANCE_ONLY')
$keys+=@('VULKANIZER_V2_FIXED_PROFILE')
$keys+=@('VULKANIZER_V2_NATIVE_VISIBILITY_PROFILE')
$keys+=@('VULKANIZER_V2_VISIBILITY_OWNERSHIP_SHADOW')
$keys+=@('VULKANIZER_V2_MIRROR_WORKLIST_AUDIT','VULKANIZER_V2_MIRROR_WORKLIST_NEGATIVE')
$keys+=@('VULKANIZER_V2_VISIBILITY_CENSUS')
$keys+=@('VULKANIZER_V2_VISIBILITY_FILTER','VULKANIZER_V2_VISIBILITY_FILTER_AUDIT','VULKANIZER_V2_VISIBILITY_FILTER_NEGATIVE','VULKANIZER_V2_PACKED_VISIBILITY_FILTER','VULKANIZER_V2_MIRROR_PREFETCH')
if ($AggressiveOwnership) { $keys+=@('VULKANIZER_V2_GPU_RESIDENT','VULKANIZER_V2_AGGRESSIVE_OWNERSHIP') }
if ($GroupRotation) { $keys+=@('VULKANIZER_V2_GROUP_ROTATION','VULKANIZER_V2_PERSISTENT_EDITS','VULKANIZER_V2_LAZY_ROUTES','VULKANIZER_V2_RETENTION_AUDIT') }
foreach ($key in $keys) { $previous[$key]=[Environment]::GetEnvironmentVariable($key,'Process'); [Environment]::SetEnvironmentVariable($key,$null,'Process') }
$runName = if ($TemporalControl) { 'control' } elseif ($TemporalReplay) { 'replay' } else { $Mode }
if ($PositionSetterAudit) { $runName = "setter-$runName" }
if ($SetterShadowGroup) {
    $shadowMode = if ($SetterShadowNative) { 'native' } else { 'retain' }
    $runName = "setter-shadow-g$SetterShadowGroup-$shadowMode"
    if ($SetterShadowAligned) { $runName += '-aligned' }
}
if ($HyperAggressive) { $runName += '-hyper' }
if ($HyperNegativeControl) { $runName += '-negative' }
if ($PerformanceOnly) { $runName += '-perf' }
if ($FixedProfile) { $runName += '-fixed-profile' }
if ($MirrorWorklistAudit) { $runName += '-mirror-audit' }
if ($MirrorWorklistNegativeControl) { $runName += '-negative' }
if ($VisibilityCensus) { $runName += '-visibility-census' }
if ($VisibilityFilterAudit) { $runName += '-visibility-filter-audit'; if (!$VisibilityFilter) { $runName += '-eager' } }
elseif ($VisibilityFilter) { $runName += '-visibility-filter' }
if ($VisibilityFilterNegativeControl) { $runName += '-negative' }
if ($PackedVisibilityFilter) { $runName += '-packed' }
if ($MirrorPrefetch) { $runName += '-prefetch' }
if ($NativeVisibilityProfile) { $runName += '-native-vis' }
if ($VisibilityOwnershipShadow) { $runName += '-own-shadow' }
$report=Join-Path $OutputDirectory "$runName.json"
$nativeTracePath=Join-Path $OutputDirectory "$runName.etl"
$nativeStackPath=Join-Path $OutputDirectory "$runName.stack-profile.txt"
$nativeFrequencyPath=Join-Path $OutputDirectory "$runName.sample-frequency.txt"
$nativeWprStarted=$false
$nativeTraceCaptured=$false
$nativeStackAvailable=$false
$nativeFunctionSymbolsAvailable=$false
$nativeSampleFrequencyHz=$null
$nativeSamplePeriod=$null
try {
    & (Join-Path $PSScriptRoot 'dev.ps1') Install -PackagePath $PackagePath
    $env:VULKANIZER_V2='1'
    if ($AggressiveOwnership) {
        $env:VULKANIZER_V2_GPU_RESIDENT='1'
        $env:VULKANIZER_V2_AGGRESSIVE_OWNERSHIP='1'
    }
    if ($GroupRotation) {
        $env:VULKANIZER_V2_GROUP_ROTATION='1'
        $env:VULKANIZER_V2_PERSISTENT_EDITS='0'
        $env:VULKANIZER_V2_LAZY_ROUTES='1'
        $env:VULKANIZER_V2_RETENTION_AUDIT='0'
    }
    $env:VULKANIZER_V2_TEST='1'
    if ($PerformanceOnly -or $FixedProfile -or $MirrorWorklistAudit -or $VisibilityCensus -or $VisibilityFilterAudit) { $env:VULKANIZER_V2_PERFORMANCE_ONLY='1' }
    if ($FixedProfile) { $env:VULKANIZER_V2_FIXED_PROFILE='1' }
    if ($NativeVisibilityProfile) {
        $env:VULKANIZER_V2_NATIVE_VISIBILITY_PROFILE='1'
        $env:VULKANIZER_V2_SPIKE_AUDIT='1'
    }
    if ($VisibilityOwnershipShadow) { $env:VULKANIZER_V2_VISIBILITY_OWNERSHIP_SHADOW='1' }
    if ($MirrorWorklistAudit) { $env:VULKANIZER_V2_MIRROR_WORKLIST_AUDIT='1' }
    if ($MirrorWorklistNegativeControl) { $env:VULKANIZER_V2_MIRROR_WORKLIST_NEGATIVE='1' }
    if ($VisibilityCensus) { $env:VULKANIZER_V2_VISIBILITY_CENSUS='1' }
    if ($VisibilityFilter) { $env:VULKANIZER_V2_VISIBILITY_FILTER='1' }
    if ($VisibilityFilterAudit) { $env:VULKANIZER_V2_VISIBILITY_FILTER_AUDIT='1' }
    if ($VisibilityFilterNegativeControl) { $env:VULKANIZER_V2_VISIBILITY_FILTER_NEGATIVE='1' }
    if ($PackedVisibilityFilter) { $env:VULKANIZER_V2_PACKED_VISIBILITY_FILTER='1' }
    if ($MirrorPrefetch) { $env:VULKANIZER_V2_MIRROR_PREFETCH='1' }
    if ($NativeCallerAudit) { $env:VULKANIZER_V2_NATIVE_CALLERS='1' }
    if ($SpikeAudit) { $env:VULKANIZER_V2_SPIKE_AUDIT='1' }
    if ($TemporalReplay) { $env:VULKANIZER_V2_TEMPORAL_REPLAY='1' }
    if ($TemporalControl) { $env:VULKANIZER_V2_TEMPORAL_CONTROL='1' }
    if ($RevocationAudit) { $env:VULKANIZER_V2_REVOCATION_AUDIT='1' }
    if ($PositionSetterAudit) { $env:VULKANIZER_V2_SETTER_AUDIT='1' }
    if ($HyperAggressive) { $env:VULKANIZER_V2_HYPER_AGGRESSIVE='1' }
    if ($HyperNegativeControl) { $env:VULKANIZER_V2_HYPER_NEGATIVE='1' }
    if ($SetterShadowGroup) {
        $env:VULKANIZER_V2_SETTER_SHADOW_GROUP=[string]$SetterShadowGroup
        if ($SetterShadowNative) { $env:VULKANIZER_V2_SETTER_SHADOW_NATIVE='1' }
        if ($SetterShadowAligned) { $env:VULKANIZER_V2_SETTER_SHADOW_ALIGNED='1' }
    }
    $env:VULKANIZER_V2_TEST_SECONDS=[string]$DurationSeconds
    $env:VULKANIZER_V2_REPORT=$report
    if ($Mode-eq'baseline') { $env:VULKANIZER_V2_BASELINE='1' }
    if ($ValidateReference) { $env:VULKANIZER_V2_VALIDATE='1' }
    if ($Census) { $env:VULKANIZER_V2_CENSUS='1' }
    if ($StructuralFixture) { $env:VULKANIZER_V2_STRUCTURAL_FIXTURE='1' }
    if ($StaticTransforms) { $env:VULKANIZER_V2_STATIC_TRANSFORMS='1' }
    if ($ColorShakeFixture) { $env:VULKANIZER_V2_COLOR_SHAKE_FIXTURE='1' }
    if ($LocalColorShake) { $env:VULKANIZER_V2_LOCAL_COLOR_SHAKE='1' }
    if ($ToggleFixture) { $env:VULKANIZER_V2_TOGGLE_FIXTURE='1' }
    if ($PersistVisibility) { $env:VULKANIZER_V2_PERSIST_VISIBILITY='1' }
    if ($LateDependencies) { $env:VULKANIZER_V2_LATE_DEPENDENCIES='1' }
    if ($Headless) { $env:VULKANIZER_V2_HEADLESS='1' }
    if ($HeadlessFixture) { $env:VULKANIZER_V2_HEADLESS_FIXTURE='1' }
    if ($HeadlessMaterial) { $env:VULKANIZER_V2_HEADLESS_MATERIAL='1' }
    if ($HeadlessAlpha) { $env:VULKANIZER_V2_HEADLESS_ALPHA='1' }
    if ($SharedHistory) { $env:VULKANIZER_V2_SHARED_HISTORY='1' }
    if ($CaptureTime -gt 0) { $env:VULKANIZER_V2_CAPTURE_TIME=$CaptureTime.ToString([Globalization.CultureInfo]::InvariantCulture) }
    if ($XLockFixture) { $env:VULKANIZER_V2_XLOCK_FIXTURE='1' }
    if ($ResolvedXLock) { $env:VULKANIZER_V2_RESOLVED_XLOCK='1' }
    if ($Capture) { $env:VULKANIZER_V2_CAPTURE=Join-Path $OutputDirectory "$runName.ppm" }
    if ($LevelName) { $env:VULKANIZER_TEST_LEVEL_NAME=$LevelName }
    $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $PSScriptRoot 'run-gd.ps1')+'"'),'-Backend',$Backend,'-MesaDirectory',('"'+(Join-Path $root 'dist/zink-clean')+'"'))
    $started=[DateTime]::UtcNow
    if ($NativeVisibilityProfile) {
        $wprCommand=Get-Command wpr.exe -ErrorAction SilentlyContinue
        if ($wprCommand) {
            $wprStartOutput=& $wprCommand.Source -start CPU -filemode 2>&1
            if ($LASTEXITCODE -eq 0) { $nativeWprStarted=$true }
            else { Write-Warning ("WPR CPU stack capture could not start; phase timers will be used. {0}" -f ($wprStartOutput -join ' ')) }
        } else { Write-Warning 'WPR is unavailable; phase timers will be used.' }
    }
    $deadlineSeconds = if ($VisibilityFilterAudit) { 960 } elseif ($SetterShadowAligned -or $HyperAggressive -or $PerformanceOnly) { 360 } else { $DurationSeconds+50 }
    $wrapper=Start-Process powershell.exe -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory "$runName.stdout.txt") -RedirectStandardError (Join-Path $OutputDirectory "$runName.stderr.txt")
    while (!$wrapper.WaitForExit(1000)) {
        if (([DateTime]::UtcNow-$started).TotalSeconds-gt$deadlineSeconds) {
            $game=Get-Process GeometryDash -ErrorAction SilentlyContinue|Where-Object {$_.StartTime.ToUniversalTime()-ge$started}
            foreach ($owned in $game) { $null=$owned.CloseMainWindow() }
            if (!$wrapper.WaitForExit(10000)) { throw 'Owned test session did not close normally. Recovery files remain in the output directory.' }
            throw 'Live V2 test exceeded its startup deadline'
        }
    }
    if ($nativeWprStarted) {
        $wprStopOutput=& $wprCommand.Source -stop $nativeTracePath 2>&1
        $wprStopExit=$LASTEXITCODE
        if ($wprStopExit -eq 0) { $nativeWprStarted=$false }
        if ($wprStopExit -eq 0 -and (Test-Path -LiteralPath $nativeTracePath)) {
            $nativeTraceCaptured=$true
            $symbolRoot=Join-Path $root 'build/RelWithDebInfo'
            $previousSymbolPath=$env:_NT_SYMBOL_PATH
            $previousSymCachePath=$env:_NT_SYMCACHE_PATH
            try {
                $env:_NT_SYMBOL_PATH=$symbolRoot
                $env:_NT_SYMCACHE_PATH=Join-Path $OutputDirectory "$runName.symcache"
                [IO.Directory]::CreateDirectory($env:_NT_SYMCACHE_PATH)|Out-Null
                $null=& xperf.exe -i $nativeTracePath -o $nativeStackPath -symbols -a profile -detail 2>&1
                if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $nativeStackPath)) {
                    $nativeStackAvailable=$true
                    $nativeStackText=Get-Content -LiteralPath $nativeStackPath -Raw
                    $nativeFunctionSymbolsAvailable=($nativeStackText -match 'us3lesscreator\.vulkanizer\.dll!(?!"Unknown")')
                } else { Write-Warning 'ETW trace was saved, but xperf could not decode its sampled profile; phase timers remain available.' }
                $frequencyOutput=& xperf.exe -i $nativeTracePath -a profile -freq 2>&1
                if ($LASTEXITCODE -eq 0) {
                    $frequencyOutput | Set-Content -LiteralPath $nativeFrequencyPath -Encoding UTF8
                    $frequencyText=$frequencyOutput -join "`n"
                    if ($frequencyText -match '(?i)([0-9]+(?:\.[0-9]+)?)\s*Hz') {
                        $nativeSampleFrequencyHz=[double]::Parse($matches[1],[Globalization.CultureInfo]::InvariantCulture)
                    }
                    if ($frequencyText -match '(?m)^FreqZone,\s*[0-9]+,\s*[0-9]+,\s*([0-9]+(?:\.[0-9]+)?)\s*$') {
                        $nativeSamplePeriod=[double]::Parse($matches[1],[Globalization.CultureInfo]::InvariantCulture)
                    }
                } else { Write-Warning 'ETW trace was saved, but its sample frequency could not be read.' }
            } catch {
                Write-Warning ("ETW profile export failed; phase timers remain available. {0}" -f $_.Exception.Message)
            } finally {
                $env:_NT_SYMBOL_PATH=$previousSymbolPath
                $env:_NT_SYMCACHE_PATH=$previousSymCachePath
            }
        } else { Write-Warning ("WPR did not save the CPU trace; phase timers will be used. {0}" -f ($wprStopOutput -join ' ')) }
    }
    $log=Get-ChildItem -LiteralPath (Join-Path $gameRoot 'geode/logs') -Filter '*.log'|Sort-Object LastWriteTime -Descending|Select-Object -First 1
    Copy-Item -LiteralPath $log.FullName -Destination (Join-Path $OutputDirectory "$runName.geode.log") -Force
    if (!(Test-Path -LiteralPath $report)-or(Get-Item -LiteralPath $report).LastWriteTimeUtc-lt$started) {
        Get-Content -LiteralPath (Join-Path $OutputDirectory "$runName.stderr.txt"); throw 'The live test produced no fresh V2 report'
    }
    $result=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
    if ($NativeVisibilityProfile) {
        $nativeProfile=$result.native_visibility_profile
        if (!$nativeProfile -or !$nativeProfile.enabled -or $nativeProfile.frames -lt 1) { throw 'Missing per-frame native visibility profile; verify the patched package was built and installed.' }
        $oldMoveVisits=$nativeProfile.PSObject.Properties['geometry_suppressed_visits']
        if ($oldMoveVisits) {
            $nativeProfile | Add-Member -NotePropertyName eliminated_move_visits -NotePropertyValue $oldMoveVisits.Value -Force
            $nativeProfile.PSObject.Properties.Remove('geometry_suppressed_visits')
        }
        $frameRows=@($nativeProfile.frames_detail)
        foreach ($frame in $frameRows) {
            $oldFrameVisits=$frame.PSObject.Properties['geometry_suppressed_visits']
            if ($oldFrameVisits) {
                $frame | Add-Member -NotePropertyName eliminated_move_visits -NotePropertyValue $oldFrameVisits.Value -Force
                $frame.PSObject.Properties.Remove('geometry_suppressed_visits')
            }
        }
        $correlationFor = {
            param([object[]]$Frames,[string]$PhaseName,[string]$CounterName)
            $sumX=0.0; $sumY=0.0; $sumXX=0.0; $sumYY=0.0; $sumXY=0.0; $count=0
            foreach ($frame in $Frames) {
                $x=[double]$frame.phase_exclusive_ms.$PhaseName
                if ($CounterName -eq 'simulation_steps') { $y=[double]$frame.simulation_steps }
                else { $y=[double]$frame.event_deltas.PSObject.Properties[$CounterName].Value }
                $sumX+=$x; $sumY+=$y; $sumXX+=$x*$x; $sumYY+=$y*$y; $sumXY+=$x*$y; $count++
            }
            $varianceX=$count*$sumXX-$sumX*$sumX; $varianceY=$count*$sumYY-$sumY*$sumY
            $available=($count -gt 1 -and $varianceX -gt 0 -and $varianceY -gt 0)
            $correlation=if($available){($count*$sumXY-$sumX*$sumY)/[Math]::Sqrt($varianceX*$varianceY)}else{$null}
            [pscustomobject]@{sample_frames=$count;available=$available;pearson_r=$correlation}
        }
        $profileCorrelations=[ordered]@{}
        $correlationCounters=@('simulation_steps','native_section_registrations','native_section_transitions',
            'sprite_batch_attachments','sprite_batch_detachments','mirror_materializations','opacity_changes','sprite_color_updates')
        foreach ($phaseName in @('stock_pre_visibility_callback','stock_visibility_callback',
                'visibility_mirror_scan','resident_sync','native_section','batch_visit')) {
            $phaseCorrelations=[ordered]@{}
            foreach ($counterName in $correlationCounters) {
                $phaseCorrelations[$counterName]=& $correlationFor $frameRows $phaseName $counterName
            }
            $profileCorrelations["${phaseName}_ms"]=$phaseCorrelations
        }
        $nativeProfile.correlations=$profileCorrelations
        $nativeProfile | Add-Member -NotePropertyName correlation_scope -NotePropertyValue 'Pearson correlations derived from per-frame exclusive scope values and event deltas; correlation does not establish causation.' -Force
        $nativeProfile | Add-Member -NotePropertyName stack_sampling -NotePropertyValue ([pscustomobject]@{
            trace_saved=$nativeTraceCaptured
            profile_export_saved=$nativeStackAvailable
            function_symbols_resolved=$nativeFunctionSymbolsAvailable
            profile='WPR CPU (CPU.Verbose.Memory)'
            sample_frequency_hz=$nativeSampleFrequencyHz
            sample_period_from_xperf=$nativeSamplePeriod
            etl_file=if ($nativeTraceCaptured) { [IO.Path]::GetFileName($nativeTracePath) } else { $null }
            stack_report=if ($nativeStackAvailable) { [IO.Path]::GetFileName($nativeStackPath) } else { $null }
            frequency_report=if (Test-Path -LiteralPath $nativeFrequencyPath) { [IO.Path]::GetFileName($nativeFrequencyPath) } else { $null }
            pmc_ipc_collected=$false
            pmc_l3_misses_collected=$false
            fallback=if ($nativeFunctionSymbolsAvailable) { $null } else { 'Exclusive fixed-profile phase timers; ETW function symbols unresolved.' }
        })
        $result | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $report -Encoding UTF8
        Write-Host "Native visibility profile: $($nativeProfile.frames) frames; pre_visibility inclusive $([Math]::Round($result.spike_audit.phase_totals.pre_visibility.inclusive_ms,2)) ms; visibility inclusive $([Math]::Round($result.spike_audit.phase_totals.visibility.inclusive_ms,2)) ms"
        if ($nativeProfile.phase_hotspots_ranked) {
            $nativeProfile.phase_hotspots_ranked | Select-Object -First 10 | Format-Table rank,phase,exclusive_ms,calls -AutoSize | Out-Host
        }
        if ($nativeTraceCaptured) { Write-Host "ETW CPU trace: $nativeTracePath" }
        if ($nativeStackAvailable) { Write-Host "ETW function samples: $nativeStackPath" }
    }
    if ($PerformanceOnly -or $FixedProfile -or $MirrorWorklistAudit -or $VisibilityCensus -or $VisibilityFilterAudit) {
        $mirror = $result.mirror_worklist
        if ($HyperAggressive) {
            if (!$mirror -or !$mirror.enabled -or $mirror.prefetch_enabled -ne [bool]$MirrorPrefetch) { throw 'Missing mirror worklist mode or prefetch setting; rebuild the mod' }
            Write-Host "Mirror worklist: $($mirror.candidate_visits) candidates vs $($mirror.old_full_scan_slots) old scan slots; $($mirror.materializations) materializations; $($mirror.empty_passes)/$($mirror.passes) empty passes; oracle $($mirror.audit_passes) passes, $($mirror.audit_misses) misses"
            Write-Host "Mirror prefetch: enabled=$($mirror.prefetch_enabled)"
            if ($mirror.first_miss.object -ne $null) { Write-Host "First mirror miss: $($mirror.first_miss | ConvertTo-Json -Compress)" }
        }
        if ($VisibilityFilter -or $VisibilityFilterAudit) {
            $vf = $result.visibility_filter
            Write-Host "Visibility filter: $($vf.eligible) eligible; $($vf.deferred_visits) deferred visits; $($vf.defer_episodes) episodes; $($vf.resumed_at_visibility) visibility resumes; $($vf.resumed_by_other_consumers) consumer resumes; $($vf.camera_candidates) camera candidates; $($vf.peak_deferred) peak deferred; $($vf.late_visible) late visible; tracking lost=$($vf.tracking_lost)"
            Write-Host "Packed predicate: enabled=$($vf.packed_enabled); comparisons=$($vf.packed_predicate_comparisons); mismatches=$($vf.packed_predicate_mismatches)"
            if ($vf.packed_first_mismatch.object_id -ne $null) { Write-Host "First packed mismatch: $($vf.packed_first_mismatch | ConvertTo-Json -Compress -Depth 8)" }
            Write-Host "Eligibility: $($vf.initial_eligible) initial -> $($vf.eligible) current; headless $($vf.initial_headless_eligible)/$($vf.initial_headless_owned); $($vf.reclassifications) reclassified; $($vf.readmitted) readmitted; $($vf.space_changes) space changes"
            Write-Host "Current classifications: $($vf.current_classifications | ConvertTo-Json -Compress)"
            Write-Host "Geometry events: $($vf.geometry_events | ConvertTo-Json -Compress)"
            if ($vf.first_miss.object -ne $null) { Write-Host "First filter miss: $($vf.first_miss | ConvertTo-Json -Compress)" }
            if (!$vf -or $vf.eligibility_revision -ne 2 -or $vf.enabled -ne [bool]$VisibilityFilter -or $vf.audit -ne [bool]$VisibilityFilterAudit -or $vf.packed_enabled -ne [bool]$PackedVisibilityFilter) { throw 'Visibility filter mode or optimization setting missing or wrong; rebuild the mod' }
            if ($vf.packed_predicate_mismatches -ne 0) { throw 'Packed visibility predicate mismatch; inspect the first mismatch in the report' }
            if ($VisibilityFilterAudit) {
                Write-Host "Audit: $($vf.trace.Count) visibility passes; $($vf.audit_checks) deferred checks; $($vf.audit_checkpoints) checkpoints; $($vf.audit_checkpoint_objects) replay checks; $($vf.audit_failures) failures"
                Write-Host "Candidate decisions: $($vf.candidate_decisions_audit_only | ConvertTo-Json -Compress)"
                Write-Host "Upload: $report"
                if ($VisibilityFilterNegativeControl) {
                    if (!$vf.negative_injected -or !$vf.negative_caught -or !$vf.audit_failures -or !$vf.failed -or $result.test_completed -or $result.render_failures -or $result.emergency_releases -or $result.ownership_revocations) { throw 'Visibility filter negative control failed' }
                    Write-Host 'PASS: intentionally deferred relevant object caught before stock visibility'
                    return
                }
                if (!$result.test_completed -or !$result.fixed_clock_frames -or !$vf.trace.Count -or $vf.failed -or $vf.audit_failures -or $vf.tracking_lost -or $result.pivotless_abort -or $result.render_failures -or $result.emergency_releases -or $result.unmodeled_attempts -or $result.rebase_rejections -or $result.ownership_revocations) { throw 'Visibility filter audit failed; inspect first miss and report' }
                if ($VisibilityFilter -and (!$vf.deferred_visits -or !$vf.resumed_at_visibility -or !$vf.audit_checkpoint_objects)) { throw 'Filter did not exercise deferred and resumed objects' }
                Write-Host 'Audit captured; compare with the opposite audit mode before claiming equality'
                return
            }
            if ($vf.late_visible -or $vf.tracking_lost) { Write-Warning 'Visibility filter encountered a late visibility activation or untracked sections; inspect the audit before trusting timings.' }
        }
        if ($VisibilityCensus) {
            Write-Host "Upload: $report"
            $vc = $result.visibility_census
            if (!$result.test_completed -or !$result.fixed_clock_frames -or !$vc.enabled -or !$vc.sampled_materializations -or $vc.population_materializations -ne $mirror.materializations -or !$vc.paired_samples -or $result.pivotless_abort -or $result.render_failures -or $result.emergency_releases -or $result.unmodeled_attempts -or $result.rebase_rejections -or $result.ownership_revocations) { throw 'Visibility census did not complete cleanly' }
            Write-Host "Visibility: $($vc.sampled_materializations) samples from $($vc.population_materializations) materializations; $($vc.paired_samples) paired; $($vc.repeat_materializations_same_frame) repeat materializations in same frame"
            $vc.regions.PSObject.Properties | ForEach-Object { [pscustomobject]@{Region=$_.Name; Samples=$_.Value.samples; Percent=[Math]::Round(100*$_.Value.samples/$vc.sampled_materializations,2); VisibleBefore=$_.Value.native_visible_before; VisibleAfter=$_.Value.native_visible_after; Entered=$_.Value.became_visible; Exited=$_.Value.became_hidden} } | Format-Table -AutoSize | Out-Host
            Write-Host "Stable far + hidden both: $($vc.far_at_old_new_and_post_visibility_position_hidden_both) samples"
            return
        }
        if ($MirrorWorklistAudit) {
            Write-Host "Upload: $report"
            if (!$result.mirror_worklist_audit -or !$result.fixed_clock_frames -or !$mirror.audit_passes) { throw 'Mirror oracle or fixed clock not exercised' }
            if ($MirrorWorklistNegativeControl) {
                if (!$mirror.negative_injected -or !$mirror.negative_caught -or !$mirror.audit_misses -or $result.test_completed -or $result.pivotless_abort -or $result.render_failures -or $result.emergency_releases) { throw 'Missed-mark negative control failed' }
                Write-Host 'PASS: planted missed dirty mark caught; run deliberately aborted after recovery'
                return
            }
            if (!$result.test_completed -or !$mirror.materializations -or !$mirror.candidate_visits -or $mirror.failed -or $mirror.audit_misses -or $result.pivotless_abort -or $result.render_failures -or $result.emergency_releases -or $result.unmodeled_attempts -or $result.rebase_rejections -or $result.ownership_revocations) { throw 'Mirror coverage audit failed or did not exercise the worklist' }
            Write-Host 'PASS: periodic old-scan oracle found zero missed materializations'
            return
        }
        if ($result.mirror_worklist_audit -or $mirror.audit_passes -or $mirror.failed) { throw 'Unexpected mirror oracle activity in performance run' }
        if ($result.pivotless_abort) { Write-Host "Upload: $report"; throw 'Angle-only replay aborted; see Geode log for probe or command failure' }
        if (!$result.performance_only -or !$result.test_completed -or $result.hyper_aggressive_enabled -ne [bool]$HyperAggressive -or $result.frame_samples -lt 1) { throw 'Missing completed performance-only run; verify patched package' }
        $measurementLabel = if ($FixedProfile) { 'Fixed-profile throughput' } else { 'Performance' }
        Write-Host "${measurementLabel}: $($result.frame_samples) frames; mean $($result.cpu_frame_mean_ms) ms; P50 $($result.cpu_frame_p50_ms) ms; P95 $($result.cpu_frame_p95_ms) ms; P99 $($result.cpu_frame_p99_ms) ms; worst $($result.cpu_frame_max_ms) ms; FPS $($result.mean_fps)"
        Write-Host "Ownership: $($result.retained_at_test_completion) retained; $($result.ownership_revocations) revocations; $($result.native_position_rebases) rebases; $($result.rebase_rejections) rejected rebases"
        if ($FixedProfile -and $VisibilityFilter) {
            $expectedMaterializations = 39992413
            $materializationDifference = [Math]::Abs([double]$mirror.materializations - $expectedMaterializations)
            if ($materializationDifference -gt 0) {
                $materializationDifferencePercent = 100 * $materializationDifference / $expectedMaterializations
                Write-Warning ("Fixed-profile materializations {0:N0} vs baseline {1:N0}: absolute difference {2:N0} ({3:N2}%)." -f
                    $mirror.materializations, $expectedMaterializations, $materializationDifference, $materializationDifferencePercent)
            }
            if ($result.ownership_revocations -or $result.rebase_rejections -or $result.render_failures -or $result.emergency_releases -or $result.unmodeled_attempts) { throw 'Optimized fixed-profile run had an ownership or reconstruction failure' }
        }
        if ($HyperAggressive) { Write-Host "Pivotless: $($result.pivotless_commands) commands; $($result.pivotless_gpu_recipients) GPU recipient visits; $($result.pivotless_native_calls) native calls; $($result.pivotless_native_materializations) lazy mirrors; $($result.pivotless_batches) shared batches; $($result.pivotless_section_events) section events; probe=$($result.pivotless_probe_passed)" }
        if (!$result.frame_trace -or !$result.step_histogram) { throw 'Missing frame step trace; rebuild with the profiling patch' }
        Write-Host "Step histogram: $($result.step_histogram | ConvertTo-Json -Compress); interval/step correlation: $($result.step_interval_correlation)"
        if ($FixedProfile) {
            if (!$result.fixed_profile -or !$result.fixed_clock_frames -or !$result.spike_audit.observed_move_steps) { throw 'Fixed-profile clock or phase profiler not exercised' }
            $phaseRows = $result.spike_audit.phase_totals.PSObject.Properties | ForEach-Object { [pscustomobject]@{Phase=$_.Name; ExclusiveMs=[Math]::Round($_.Value.exclusive_ms,2); Percent=[Math]::Round(100*$_.Value.exclusive_ms/$result.spike_audit.wall_ms,2)} }
            Write-Host 'Fixed 30 Hz director input: rank by exclusive CPU wall time (no double counting)'
            $phaseRows | Sort-Object ExclusiveMs -Descending | Select-Object -First 12 | Format-Table -AutoSize | Out-Host
            Write-Host "Mirror consumers: $($result.mirrors_by_consumer | ConvertTo-Json -Compress -Depth 5)"
            if ($VisibilityOwnershipShadow) {
                $ownership = $result.visibility_ownership_shadow
                if (!$ownership -or !$ownership.enabled -or !$ownership.frames) { throw 'Missing per-frame visibility ownership shadow report; verify the patched package was built and installed.' }
                Write-Host "Visibility ownership shadow: $($ownership.section_index.currently_indexed_owned_objects) indexed; $($ownership.visibility.candidate_passes) candidate passes; current candidates $($ownership.visibility.conservative_candidate_count); visible owned $($ownership.visibility.currently_native_visible_owned); exact predictions unavailable"
                Write-Host "Candidate coverage: $($ownership.visibility.candidate_visible_coverage_observations) visible covered; overcoverage $($ownership.visibility.candidate_overcoverage_observations); visible outside candidate $($ownership.visibility.visible_owned_outside_candidate_observations)"
                Write-Host "Owned transitions: $($ownership.owned_batch_attachments) batch attaches; $($ownership.owned_batch_detachments) detaches; section transitions $($ownership.section_index.owned_section_transitions); detach/reattach $($ownership.detach_to_reattach_interval_frames_histogram | ConvertTo-Json -Compress)"
                Write-Host "Negative control: caught=$($ownership.negative_control.incorrect_prediction_detected); checks=$($ownership.negative_control.injected_mismatches_checked); section mismatches=$($ownership.section_membership_mismatch_unique_objects)"
                if (!$ownership.negative_control.incorrect_prediction_detected) { Write-Warning 'The visibility comparison negative control did not find a visible candidate during the measured run.' }
            }
        } elseif ($result.fixed_profile) { throw 'PerformanceOnly unexpectedly used the fixed clock' }
        Write-Host "Upload: $report"
        if ($result.render_failures -or $result.emergency_releases -or $result.unmodeled_attempts -or $result.rebase_rejections) { Write-Warning 'Performance run encountered an operational failure; not a correctness result.' }
        return
    }
    if ($HyperAggressive) {
        $hyper=$result.hyper_aggressive
        $compact = Join-Path $OutputDirectory "$runName.hyper-summary.txt"
        $result | ConvertTo-Json -Depth 100 -Compress | Set-Content -LiteralPath $compact -Encoding UTF8
        if (!$hyper -or !$hyper.enabled) { throw 'Hyper mode not active in patched package' }
        Write-Host "Hyper: $($hyper.policy_attempts_retained) blocked object-handoffs; $($hyper.unmodeled_attempts) unmodeled; $($hyper.emergency_released) emergency releases; $($hyper.pivotless_checks) pivotless checks; $($hyper.angle_failures) angle mismatches; $($hyper.setter_failures) setter mismatches"
        Write-Host "Pivotless: $($hyper.pivotless_commands) commands; $($hyper.pivotless_gpu_recipients) GPU recipient visits; $($hyper.pivotless_native_calls) native calls; $($hyper.pivotless_native_materializations) lazy mirrors; $($hyper.pivotless_batches) shared batches; $($hyper.pivotless_section_events) section events; probe=$($hyper.pivotless_probe_passed)"
        Write-Host "Native rebases: $($hyper.native_position_rebases) across $($hyper.native_rebase_unique_objects) objects; $($hyper.native_rebase_validation_checks) GPU checks; $($hyper.native_rebase_validation_failures) failures; before/during visibility $($hyper.native_rebase_diverged_before_visibility)/$($hyper.native_rebase_diverged_during_visibility)"
        if ($hyper.native_position_rebases -gt 0 -and $hyper.native_rebase_validation_checks -eq 0) { Write-Warning 'Native rebases were not read back for validation' }
        Write-Host "Upload: $compact"
        if ($hyper.pivotless_abort -or $hyper.pivotless_section_events) { throw 'Angle-only replay aborted; see Geode log for probe or command failure' }
        if ($HyperNegativeControl -and (!$hyper.negative_injected -or !$hyper.negative_caught)) { throw 'Negative control was not caught' }
    }
    if (!$result.test_completed) { throw 'The test did not reach its completion checkpoint' }
    if (!$LevelName-and$Mode-eq'shared'-and!$AllowNoEliminated-and($result.peak_owned-le0-or$result.v2_draws-le0-or$result.eliminated_native_move_member_visits-le0)) { throw 'The controlled run did not exercise shared translations' }
    if ($result.render_failures-ne0-or$result.v2_native_commit_violations-ne0) {
        if (!$HyperAggressive) { throw 'An ownership or rendering invariant failed' }
        Write-Warning 'Hyper run has rendering or ownership invariant failures; diagnostics only.'
    }
    if ($TemporalReplay) {
        $capacity = if ($TemporalControl) { 256 } else { 2048 }
        if (!$result.temporal_replay -or $result.temporal_replay.capacity -ne $capacity -or $result.temporal_replay.ordered_batches -lt 1) {
            throw 'Temporal replay did not exercise the requested command capacity'
        }
        if ($ValidateReference -and ($result.resident_gpu.validation_checks -lt 1 -or $result.resident_gpu.exactness_violations -ne 0)) {
            if (!$SetterShadowGroup -and !$HyperAggressive) { throw 'Temporal replay failed reference validation or did not exercise it' }
            Write-Warning "Temporal replay failed reference validation or did not exercise it; preserving shadow diagnostics."
        }
    }
    if ($Capture) {
        $image=Join-Path $OutputDirectory "$runName.ppm"
        if (!(Test-Path -LiteralPath $image)-or(Get-Item -LiteralPath $image).LastWriteTimeUtc-lt$started) { throw 'Missing fresh validation image' }
    }
    if ($RevocationAudit) {
        if (!$result.revocation_audit -or !$result.revocation_audit.count_reconciled -or
            $result.revocation_audit.totals.objects -ne $result.resident_gpu.ownership_revocations) {
            throw 'Revocation audit missing or counts do not reconcile with actual ownership releases'
        }
        Write-Host "Revocations: $($result.revocation_audit.totals.objects); unwound $($result.revocation_audit.unwound_objects); heuristic $($result.revocation_audit.heuristic_objects); unresolved $($result.revocation_audit.unresolved_objects); diagnostic overhead $($result.revocation_audit.audit_wall_ms) ms"
    }
    if ($PositionSetterAudit) {
        $setter = $result.position_setter_audit
        $trace = Join-Path $OutputDirectory "$runName.setter-events.tsv"
        if (!$setter -or $setter.all.calls -lt 1 -or $setter.incomplete -ne 0 -or !$setter.trace_written -or
            $setter.trace_rows -ne $setter.all.calls -or !(Test-Path -LiteralPath $trace) -or
            (Get-Item -LiteralPath $trace).LastWriteTimeUtc -lt $started) {
            throw 'Position setter audit missing, incomplete, or trace not saved'
        }
        Write-Host "Setter: $($setter.all.calls) owned calls; identical $($setter.all.buckets.bitwise_identical_input_and_logical); different input/unchanged logical $($setter.all.buckets.different_input_unchanged_logical); changed logical $($setter.all.buckets.logical_position_changed); strict candidates $($setter.all.strict_candidates)"
        Write-Host "Raw events: $trace"
    }
    $shadowFailed=$false
    if ($SetterShadowGroup) {
        $shadow=$result.setter_shadow
        $samplePath=Join-Path $OutputDirectory "$runName.shadow-samples.tsv"
        if (!$shadow -or $shadow.group -ne $SetterShadowGroup -or $shadow.native_reference -ne [bool]$SetterShadowNative -or
            $shadow.target_population -lt 1 -or $shadow.sample_rows -lt 1 -or !$shadow.sample_written -or
            !(Test-Path -LiteralPath $samplePath) -or (Get-Item -LiteralPath $samplePath).LastWriteTimeUtc -lt $started) {
            throw 'Missing or incomplete shadow diagnostics'
        }
        Write-Host "Shadow g$SetterShadowGroup ($shadowMode): $($shadow.retained_calls) retained calls; $($shadow.unique_retained_objects) unique; $($shadow.later_step_calls) later-step calls; $($shadow.retained_objects_still_owned) still owned"
        Write-Host "Shadow failures: setter reference $($shadow.setter_reference_mismatches); visibility reference $($shadow.visibility_reference_mismatches); postcondition $($shadow.postcondition_failures); GPU exactness $($result.resident_gpu.exactness_violations)"
        Write-Host "Shadow samples: $samplePath"
        if ($SetterShadowAligned -and (!$shadow.aligned_mode -or $shadow.aligned_clock_frames -lt 1 -or $shadow.aligned_final_step -lt 1)) {
            throw 'The aligned diagnostic clock or step counter was not exercised'
        }
        $shadowFailed = ($shadow.setter_reference_mismatches -ne 0 -or $shadow.visibility_reference_mismatches -ne 0 -or
            $shadow.postcondition_failures -ne 0 -or $result.resident_gpu.exactness_violations -ne 0 -or
            ($SetterShadowNative -and $shadow.target_owned_at_completion -ne 0))
        if (!$SetterShadowNative -and ($shadow.retained_calls -lt 1 -or $shadow.later_step_calls -lt 1)) {
            Write-Warning 'Retention was not exercised across later simulation steps; this is a smoke test only, not a correctness pass.'
        }
        if ($result.resident_gpu.validation_checks -lt 1) { Write-Warning 'GPU reference validation was not exercised.' }
    }
    $sources=[ordered]@{}
    $paths=@('src/hooks/CpuRenderPhaseHooks.cpp','src/hooks/RenderHooks.cpp','src/core/RendererManager.cpp','src/backend/RotationScheduler.cpp','mod.json')
    $paths+=@(Get-ChildItem -LiteralPath (Join-Path $root 'src/v2') -File|ForEach-Object { $_.FullName.Substring($root.Length+1).Replace('\','/') })
    foreach ($path in $paths) { $sources[$path]=(Get-FileHash -LiteralPath (Join-Path $root $path)).Hash }
    $execution=[ordered]@{timestamp_utc=[DateTime]::UtcNow.ToString('o');mode=$Mode;level=$LevelName;backend=$Backend;reference_validation=[bool]$ValidateReference;package_sha256=(Get-FileHash -LiteralPath $installed).Hash;source_file_hashes_sha256=$sources}
    $execution|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $OutputDirectory "$runName.execution.json") -Encoding UTF8
    if ($SpikeAudit) {
        if (!$result.spike_audit -or $result.spike_audit.frames -lt 1 -or @($result.spike_audit.worst_frames).Count -lt 1) {
            throw 'Missing completed spike audit. Verify that the patched package was built and installed.'
        }
        $suffix = if ($PositionSetterAudit) { 'summary' } elseif ($RevocationAudit) { 'revocations' } else { 'spike' }
        $compact = Join-Path $OutputDirectory "$runName.$suffix.txt"
        $result | ConvertTo-Json -Depth 100 -Compress | Set-Content -LiteralPath $compact -Encoding UTF8
        Write-Host "Spike audit: $($result.spike_audit.frames) frames; worst $($result.spike_audit.worst_frames[0].wall_ms) ms"
        Write-Host "Upload: $compact"
    } else { Get-Content -LiteralPath $report }
    if ($shadowFailed) { throw 'Shadow invariant failed; send the saved reports before continuing' }
} finally {
    if ($nativeWprStarted) {
        $null=& $wprCommand.Source -stop $nativeTracePath 2>&1
        $nativeWprStarted=$false
    }
    if (!(Get-Process GeometryDash -ErrorAction SilentlyContinue)) {
        & (Join-Path $PSScriptRoot 'dev.ps1') Restore -PackagePath $backup
        [IO.File]::WriteAllBytes($settings,$originalSettings)
    }
    foreach ($key in $keys) { [Environment]::SetEnvironmentVariable($key,$previous[$key],'Process') }
}
