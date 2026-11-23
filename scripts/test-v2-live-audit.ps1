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
    [switch]$AllowNoEliminated,
    [string]$PackagePath
)
$ErrorActionPreference='Stop'
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
if ($AggressiveOwnership) { $keys+=@('VULKANIZER_V2_GPU_RESIDENT','VULKANIZER_V2_AGGRESSIVE_OWNERSHIP') }
if ($GroupRotation) { $keys+=@('VULKANIZER_V2_GROUP_ROTATION','VULKANIZER_V2_PERSISTENT_EDITS','VULKANIZER_V2_LAZY_ROUTES','VULKANIZER_V2_RETENTION_AUDIT') }
foreach ($key in $keys) { $previous[$key]=[Environment]::GetEnvironmentVariable($key,'Process'); [Environment]::SetEnvironmentVariable($key,$null,'Process') }
$report=Join-Path $OutputDirectory "$Mode.json"
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
        $env:VULKANIZER_V2_RETENTION_AUDIT='1'
    }
    $env:VULKANIZER_V2_TEST='1'
    if ($NativeCallerAudit) { $env:VULKANIZER_V2_NATIVE_CALLERS='1' }
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
    if ($Capture) { $env:VULKANIZER_V2_CAPTURE=Join-Path $OutputDirectory "$Mode.ppm" }
    if ($LevelName) { $env:VULKANIZER_TEST_LEVEL_NAME=$LevelName }
    $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $PSScriptRoot 'run-gd.ps1')+'"'),'-Backend',$Backend,'-MesaDirectory',('"'+(Join-Path $root 'dist/zink-clean')+'"'))
    $started=[DateTime]::UtcNow
    $wrapper=Start-Process powershell.exe -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory "$Mode.stdout.txt") -RedirectStandardError (Join-Path $OutputDirectory "$Mode.stderr.txt")
    while (!$wrapper.WaitForExit(1000)) {
        if (([DateTime]::UtcNow-$started).TotalSeconds-gt$DurationSeconds+50) {
            $game=Get-Process GeometryDash -ErrorAction SilentlyContinue|Where-Object {$_.StartTime.ToUniversalTime()-ge$started}
            foreach ($owned in $game) { $null=$owned.CloseMainWindow() }
            if (!$wrapper.WaitForExit(10000)) { throw 'Owned test session did not close normally. Recovery files remain in the output directory.' }
            throw 'Live V2 test exceeded its startup deadline'
        }
    }
    $log=Get-ChildItem -LiteralPath (Join-Path $gameRoot 'geode/logs') -Filter '*.log'|Sort-Object LastWriteTime -Descending|Select-Object -First 1
    Copy-Item -LiteralPath $log.FullName -Destination (Join-Path $OutputDirectory "$Mode.geode.log") -Force
    if (!(Test-Path -LiteralPath $report)-or(Get-Item -LiteralPath $report).LastWriteTimeUtc-lt$started) {
        Get-Content -LiteralPath (Join-Path $OutputDirectory "$Mode.stderr.txt"); throw 'The live test produced no fresh V2 report'
    }
    $result=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
    if (!$result.test_completed) { throw 'The test did not reach its completion checkpoint' }
    if (!$LevelName-and$Mode-eq'shared'-and!$AllowNoEliminated-and($result.peak_owned-le0-or$result.v2_draws-le0-or$result.eliminated_native_move_member_visits-le0)) { throw 'The controlled run did not exercise shared translations' }
    if ($result.render_failures-ne0-or$result.v2_native_commit_violations-ne0) { throw 'An ownership or rendering invariant failed' }
    if ($Capture) {
        $image=Join-Path $OutputDirectory "$Mode.ppm"
        if (!(Test-Path -LiteralPath $image)-or(Get-Item -LiteralPath $image).LastWriteTimeUtc-lt$started) { throw 'Missing fresh validation image' }
    }
    $sources=[ordered]@{}
    $paths=@('src/hooks/CpuRenderPhaseHooks.cpp','src/hooks/RenderHooks.cpp','src/core/RendererManager.cpp','src/backend/RotationScheduler.cpp','mod.json')
    $paths+=@(Get-ChildItem -LiteralPath (Join-Path $root 'src/v2') -File|ForEach-Object { $_.FullName.Substring($root.Length+1).Replace('\','/') })
    foreach ($path in $paths) { $sources[$path]=(Get-FileHash -LiteralPath (Join-Path $root $path)).Hash }
    $execution=[ordered]@{timestamp_utc=[DateTime]::UtcNow.ToString('o');mode=$Mode;level=$LevelName;backend=$Backend;reference_validation=[bool]$ValidateReference;package_sha256=(Get-FileHash -LiteralPath $installed).Hash;source_file_hashes_sha256=$sources}
    $execution|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $OutputDirectory "$Mode.execution.json") -Encoding UTF8
    Get-Content -LiteralPath $report
} finally {
    if (!(Get-Process GeometryDash -ErrorAction SilentlyContinue)) {
        & (Join-Path $PSScriptRoot 'dev.ps1') Restore -PackagePath $backup
        [IO.File]::WriteAllBytes($settings,$originalSettings)
    }
    foreach ($key in $keys) { [Environment]::SetEnvironmentVariable($key,$previous[$key],'Process') }
}

