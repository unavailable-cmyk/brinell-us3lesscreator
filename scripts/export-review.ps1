param([string]$OutputPath='Vulkanizer-review.zip',[switch]$CheckpointOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$files=[ordered]@{}
$extensions=@('.cpp','.hpp','.h','.c','.glsl','.comp','.vert','.frag','.cmake','.txt','.md','.ps1','.psm1','.py','.wprp','.ini','.json','.yaml','.yml','.toml')
foreach($directory in @('src','shaders','cmake','integration','tests','scripts')){
 foreach($file in Get-ChildItem -LiteralPath (Join-Path $root $directory) -Recurse -File){
  $relative=$file.FullName.Substring($root.Length+1).Replace('\','/')
  if($file.Extension-notin$extensions-or$relative-match '(^|/)(build[^/.]*|dist|archive|external|_deps|__pycache__|\.git|node_modules)/'){continue}
  if($relative-match '(?i)(^|/)(credentials?|secrets?|\.env)(\.|/|$)'){throw "Excluded sensitive filename: $relative"}
  $files[$relative]=$file.FullName
 }
}
foreach($name in @('CMakeLists.txt','mod.json','README.md','docs/development-state.md','docs/architecture.md','docs/legacy-pipeline-boundary.md','docs/vulk-v2.md','docs/vulk-v2-live.md','docs/vulk-v2-dependency-census.md','docs/vulk-v2-staged-requirements.md','docs/vulk-v2-3b-continuation.md','docs/chain-eligibility-census.md','docs/gd-vulkan-integration.md','docs/rotation-scheduler-integration.md','docs/transform-culling.md','docs/gpu-state-parity-fix.md','docs/gameplay-speed-regression.md')){
 $path=Join-Path $root $name;if(!(Test-Path -LiteralPath $path)){throw "Required review file missing: $name"};$files[$name]=$path
}
if (!$CheckpointOnly) {
foreach ($name in @('baseline.json','shared.json','baseline.execution.json','shared.execution.json','baseline.ppm','shared.ppm','parity.json')) {
 $path=Join-Path $root "build/diagnostic-reports/v2-3b-release/$name"
 if (!(Test-Path -LiteralPath $path)) { throw "3B fixture evidence missing: $name" }
 $files["review/v2-3b-fixture/$name"]=$path
}
foreach ($name in @('baseline.json','baseline.execution.json')) {
 $path=Join-Path $root "build/diagnostic-reports/v2-3b-release-census/$name"
 if (!(Test-Path -LiteralPath $path)) { throw "3B census evidence missing: $name" }
 $files["review/v2-3b-census/$name"]=$path
}
$files['docs/vulk-v2-3b-live-expansion.md']=Join-Path $root 'docs/vulk-v2-3b-live-expansion.md'
$files['docs/vulk-v2-native-trigger-integration.md']=Join-Path $root 'docs/vulk-v2-native-trigger-integration.md'
$files['docs/vulk-v2-workload-separation.md']=Join-Path $root 'docs/vulk-v2-workload-separation.md'
$files['docs/vulk-v2-3b-implementation-mandate.md']=Join-Path $root 'docs/vulk-v2-3b-implementation-mandate.md'
foreach ($run in @('v2-mandate-final-static','v2-mandate-final-color','v2-mandate-toggle-hidden')) {
 foreach ($name in @('baseline.json','shared.json','baseline.execution.json','shared.execution.json','baseline.ppm','shared.ppm','parity.json')) {
  $path=Join-Path $root "build/diagnostic-reports/$run/$name"
  if (!(Test-Path -LiteralPath $path)) { throw "Milestone 3B fixture evidence missing: $path" }
  $files["review/$run/$name"]=$path
 }
}
foreach ($pair in @(@('v2-mandate-final-peaceful','shared'),@('v2-mandate-toggle-native','baseline'))) {
 foreach ($suffix in @('.json','.execution.json')) {
  $name=$pair[1]+$suffix; $path=Join-Path $root "build/diagnostic-reports/$($pair[0])/$name"
  if (!(Test-Path -LiteralPath $path)) { throw "Milestone 3B native evidence missing: $path" }
  $files["review/$($pair[0])/$name"]=$path
 }
}
$files['review/v2-mandate-atlas-contract.json']=Join-Path $root 'build/diagnostic-reports/v2-mandate-atlas-contract.json'
foreach ($name in @('baseline.json','shared.json','baseline.execution.json','shared.execution.json','baseline.ppm','shared.ppm','parity.json')) {
 $path=Join-Path $root "build/diagnostic-reports/v2-workload-verified-fixture/$name"
 if (!(Test-Path -LiteralPath $path)) { throw "Workload fixture evidence missing: $path" }
 $files["review/v2-workload-fixture/$name"]=$path
}
foreach ($name in @('shared.json','shared.execution.json')) {
 $path=Join-Path $root "build/diagnostic-reports/v2-workload-verified-census/$name"
 if (!(Test-Path -LiteralPath $path)) { throw "Workload census evidence missing: $path" }
 $files["review/v2-workload-census/$name"]=$path
}
foreach ($name in @('baseline.json','shared.json','baseline.execution.json','shared.execution.json','baseline.ppm','shared.ppm','parity.json')) {
 foreach ($run in @('v2-native-xlock-production','v2-native-xlock-default')) {
  $path=Join-Path $root "build/diagnostic-reports/$run/$name"
  if (!(Test-Path -LiteralPath $path)) { throw "Validated player-lock fixture evidence missing: $path" }
  $files["review/$run/$name"]=$path
 }
}
foreach ($pair in @(@('v2-native-xlock-census','shared'),@('v2-native-xlock-short','baseline'),@('v2-native-xlock-short','shared'))) {
 foreach ($suffix in @('.json','.execution.json')) {
  $name=$pair[1]+$suffix; $path=Join-Path $root "build/diagnostic-reports/$($pair[0])/$name"
  if (!(Test-Path -LiteralPath $path)) { throw "Player-lock level evidence missing: $path" }
  $files["review/$($pair[0])/$name"]=$path
 }
}
foreach ($name in @('baseline.json','shared.json','baseline.execution.json','shared.execution.json','baseline.ppm','shared.ppm','parity.json')) {
 foreach ($run in @('v2-native-xlock-atlas','v2-native-xlock-guard')) {
  $path=Join-Path $root "build/diagnostic-reports/$run/$name"
  if (!(Test-Path -LiteralPath $path)) { throw "Native trigger fixture evidence missing: $path" }
  $files["review/$run/$name"]=$path
 }
}
foreach ($pair in @(@('v2-native-xlock-protected','shared'),@('v2-native-xlock-repeat','baseline'))) {
 foreach ($suffix in @('.json','.execution.json','.ppm')) {
  $name=$pair[1]+$suffix; $path=Join-Path $root "build/diagnostic-reports/$($pair[0])/$name"
  if (!(Test-Path -LiteralPath $path)) { throw "Native trigger control evidence missing: $path" }
  $files["review/$($pair[0])/$name"]=$path
 }
 $files["review/$($pair[0])/parity.json"]=Join-Path $root "build/diagnostic-reports/$($pair[0])/parity.json"
}
foreach ($name in @('baseline.json','shared.json','baseline.execution.json','shared.execution.json')) {
 $path=Join-Path $root "build/diagnostic-reports/v2-trigger-short-final/$name"
 if (!(Test-Path -LiteralPath $path)) { throw "Short benchmark evidence missing: $path" }
 $files["review/v2-trigger-short/$name"]=$path
}
foreach ($name in @('baseline.json','shared.json','baseline.execution.json','shared.execution.json','baseline.ppm','shared.ppm','parity.json')) {
 $path=Join-Path $root "build/diagnostic-reports/v2-3b-optin/$name"
 if (!(Test-Path -LiteralPath $path)) { throw "3B opt-in fixture evidence missing: $name" }
 $files["review/v2-3b-optin/$name"]=$path
}
foreach ($pair in @(@('v2-3b-optin-disabled','baseline'),@('v2-3b-census-optin','shared'))) {
 foreach ($suffix in @('.json','.execution.json')) {
  $name=$pair[1]+$suffix; $path=Join-Path $root "build/diagnostic-reports/$($pair[0])/$name"
  if (!(Test-Path -LiteralPath $path)) { throw "3B opt-in control evidence missing: $path" }
  $files["review/$($pair[0])/$name"]=$path
 }
}
foreach ($name in @('baseline.json','baseline.execution.json')) {
 $path=Join-Path $root "build/diagnostic-reports/v2-census/$name"
 if (!(Test-Path -LiteralPath $path)) { throw "V2 census evidence missing: $name" }
 $files["review/v2-census/$name"]=$path
}
foreach ($name in @('final/shared.json','final/baseline.json','final/parity.json','final/shared.execution.json','final/baseline.execution.json','peaceful/shared.json','peaceful/shared.execution.json')) {
 $path=Join-Path $root "build/diagnostic-reports/v2-live/$name"
 if (!(Test-Path -LiteralPath $path)) { throw "Live V2 evidence missing: $name" }
 $files["review/v2-live/$name"]=$path
}
foreach($file in Get-ChildItem -LiteralPath $root -File -Filter 'LICENSE*'){$files[$file.Name]=$file.FullName}
foreach($name in @('reset-performance.json','reset-costs.json','v2-cpu-benchmark.json','v2-gl-benchmark.json','v2-benchmark-run.json','v2-validation.json')){
 $path=Join-Path $root "build/diagnostic-reports/$name"
 if(!(Test-Path $path)){throw "Benchmark summary missing: $name"}
 $files["review/benchmarks/$name"]=$path
}
$v2Run=Get-Content -LiteralPath (Join-Path $root 'build/diagnostic-reports/v2-benchmark-run.json') -Raw|ConvertFrom-Json
if (!$v2Run.source_file_hashes_sha256) { throw 'V2 benchmark source hashes missing' }
$v2Differences=@()
foreach ($source in $v2Run.source_file_hashes_sha256.PSObject.Properties) {
 if (!$files.Contains($source.Name)) { throw "V2 benchmark source excluded from archive: $($source.Name)" }
 if ((Get-FileHash -LiteralPath $files[$source.Name]).Hash-ne$source.Value) { $v2Differences+=$source.Name }
}
} else {
 $name='review/v2-alpha-history/checkpoint.json'
 $path=Join-Path $root 'build/diagnostic-reports/v2-alpha-history-checkpoint.json'
 if (!(Test-Path -LiteralPath $path)) { throw "Checkpoint evidence missing: $path" }
 $files[$name]=$path
}
$hashes=[ordered]@{}
foreach($name in @($files.Keys|Sort-Object)){
 if ([IO.Path]::GetExtension($name) -ne '.ppm') {
  $content=[IO.File]::ReadAllText($files[$name])
  if($content-match '-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|\b(?:AKIA|ASIA)[A-Z0-9]{16}\b|\bgh[pousr]_[A-Za-z0-9]{30,}\b|\bsk-proj-[A-Za-z0-9_-]{30,}\b'){throw "Potential credential found; export stopped: $name"}
 }
 $hashes[$name]=(Get-FileHash -LiteralPath $files[$name] -Algorithm SHA256).Hash
}
$mod=Get-Content (Join-Path $root 'mod.json') -Raw|ConvertFrom-Json
$manifest=[ordered]@{
 snapshot_timestamp_utc=[DateTime]::UtcNow.ToString('o');mod_version=$mod.version;geode_version=$mod.geode;gd_version=$mod.gd
 source_file_hashes_sha256=$hashes
 active_experimental_features=@{live_game_route='experimental_v2_opt_in_with_native_fallback';v2_route='live_shared_translation' ;default_evaluator='legacy_GPU';export_process_cpu_override=($env:VULKANIZER_VISUAL_CPU_ONLY-eq'1');opt_in_settings=@($mod.settings.PSObject.Properties|Where-Object{$_.Name-like'experimental-*'}|ForEach-Object{@{name=$_.Name;default=$_.Value.default}});runtime_settings='Machine-local runtime settings excluded'}
 current_validated_benchmark=@{cpu_summary='review/benchmarks/v2-cpu-benchmark.json';gpu_summary='review/benchmarks/v2-gl-benchmark.json';run='review/benchmarks/v2-benchmark-run.json';latest_validation='review/benchmarks/v2-validation.json';source_differences_since_measured_run=$v2Differences;scope='Earlier controlled V2 snapshot at 1, 1000, 10000 and 100000 textured quads. Final CPU/GPU benchmark executables blocked by Windows Application Control. Final scene correctness test passes. Source differences and final validation recorded explicitly.'}
 previous_game_benchmark=@{summary='review/benchmarks/reset-performance.json';cost_summary='review/benchmarks/reset-costs.json';scope='Legacy Peaceful level time 80-90 seconds; GPU 28.16 FPS, CPU 27.33 FPS; historical recovery-path comparison'}
 known_regressions_and_limitations=@('Windows Application Control blocked final CPU/GPU benchmark executables; archived measurements retain their earlier source provenance','Live shared translation integration requires broader gameplay, reset and context validation before promotion','Peaceful 60 FPS and POCO X7 Pro performance remain unverified','Path B independent GPU evaluator deferred; candidates retain native ownership','Controlled numerical domain; general large-coordinate and cancellation-heavy fidelity requires validation','Visibility invalidation and reset can visit scene objects outside the routine transformation path','Legacy game route retains packing, synchronization, readback and CPU commits','Full identical-input gameplay replay and whole-level visual parity remain unverified','Desktop adapter targets Windows GL 3.3; GLES 3.0 UBO alternative identified; Android adapter pending','External dependencies and toolchain excluded')
 current_architectural_milestone='3B incomplete: opt-in generalized baked transforms and limited localized color/shake passed controlled checks; native toggle moves while disabled; atlas child removal hands off whole atlas; Peaceful ownership and eliminated work remain zero'
 archive_scope='Source review snapshot, not a standalone offline SDK or executable distribution'
}
if ($CheckpointOnly) {
 $manifest.current_architectural_milestone='3B checkpoint: experimental localized alpha and live shared history increase measured Peaceful movement elimination'
 $manifest.current_validated_benchmark=@{run='review/v2-alpha-history/checkpoint.json';scope='Matched native gameplay fixture and six-second Peaceful comparison on the checkpoint build'}
 $manifest.Remove('previous_game_benchmark')
}
if(![IO.Path]::IsPathRooted($OutputPath)){$OutputPath=Join-Path $root $OutputPath}
$stream=[IO.File]::Open($OutputPath,[IO.FileMode]::Create,[IO.FileAccess]::Write)
try {
 $zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$true)
 try {
  foreach($name in @($files.Keys|Sort-Object)){[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$files[$name],$name,[IO.Compression.CompressionLevel]::Optimal)|Out-Null}
  $entry=$zip.CreateEntry('review-manifest.json');$writer=[IO.StreamWriter]::new($entry.Open(),[Text.UTF8Encoding]::new($false))
  try{$writer.Write(($manifest|ConvertTo-Json -Depth 12))}finally{$writer.Dispose()}
 }finally{$zip.Dispose()}
}finally{$stream.Dispose()}
Write-Output "Review archive: $OutputPath ($($files.Count) files plus manifest)"
