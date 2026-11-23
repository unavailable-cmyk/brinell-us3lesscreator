param([string]$ArchivePath=(Join-Path (Split-Path $PSScriptRoot -Parent) 'Vulkanizer-review.zip'))
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::OpenRead($ArchivePath)
try {
 $entry=$zip.GetEntry('review-manifest.json');if(!$entry){throw 'Missing manifest'}
 $reader=[IO.StreamReader]::new($entry.Open())
 try{$manifest=$reader.ReadToEnd()|ConvertFrom-Json}finally{$reader.Dispose()}
 if(!$manifest.snapshot_timestamp_utc-or!$manifest.current_architectural_milestone-or!$manifest.current_validated_benchmark){throw 'Incomplete manifest'}
 $expected=@($manifest.source_file_hashes_sha256.PSObject.Properties)
 if($zip.Entries.Count-ne$expected.Count+1){throw 'Unexpected/missing archive entries'}
 foreach($file in $expected){
  if($file.Name-match '(^|/)(build[^/.]*|archive|\.tools|_deps|__pycache__)/|\.(dll|exe|geode|pyc|bak|log)$|(^/|^[A-Za-z]:|\.\.)'){throw "Excluded or unsafe path: $($file.Name)"}
  $entry=$zip.GetEntry($file.Name);if(!$entry){throw "Missing source: $($file.Name)"}
  $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
  try{$actual=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')}finally{$sha.Dispose();$stream.Dispose()}
  if($actual-ne$file.Value){throw "Hash mismatch: $($file.Name)"}
 }
 foreach ($name in @('src/v2/Scene.hpp','src/v2/Scene.cpp','src/v2/GlRenderer.hpp','src/v2/GlRenderer.cpp','docs/vulk-v2.md','scripts/benchmark-v2.ps1')) {
  if (!$zip.GetEntry($name)) { throw "Missing V2 deliverable: $name" }
 }
 function ReadReport([string]$Name) {
  $reportEntry=$zip.GetEntry("review/benchmarks/$Name")
  if (!$reportEntry) { throw "Missing benchmark: $Name" }
  $reportReader=[IO.StreamReader]::new($reportEntry.Open())
  try { return $reportReader.ReadToEnd()|ConvertFrom-Json } finally { $reportReader.Dispose() }
 }
 $gpu=ReadReport 'v2-gl-benchmark.json'
 $cpu=ReadReport 'v2-cpu-benchmark.json'
 $run=ReadReport 'v2-benchmark-run.json'
 foreach ($count in @(1,1000,10000,100000)) {
  if (@($gpu.cases|Where-Object objects -eq $count).Count-ne2) { throw "Missing GPU scale cases for $count" }
  if (@($cpu.cases|Where-Object { $_.objects-eq$count-and!$_.active_chain_sweep }).Count-ne2) { throw "Missing CPU scale cases for $count" }
 }
 foreach ($case in $gpu.cases) {
  if ($case.frames-ne240-or$case.routine_object_visits-ne0-or$case.max_position_error-ne0-or$case.image_mismatched_channels-ne0-or$case.colored_pixels-le0) { throw 'V2 correctness/scaling evidence failed' }
  if ($case.draw_count-ne240-or$case.dispatch_count-ne0) { throw 'Unexpected controlled draw/dispatch count' }
  if ($case.routine_palette_upload_bytes-ne$case.chains*16*240) { throw 'Unexpected palette upload count' }
  if ($case.routine_upload_bytes-ne($case.routine_palette_upload_bytes+$case.routine_draw_uniform_upload_bytes)) { throw 'Incomplete upload accounting' }
 }
 if ($gpu.ordered_material_visibility_reset_tests-ne'pass'-or$gpu.fallback_lifetime_tests-ne'pass'-or$gpu.palette_paging_test-ne'pass') { throw 'Missing V2 integration fixture results' }
 $differences=@()
 foreach ($source in $run.source_file_hashes_sha256.PSObject.Properties) {
  if ($manifest.source_file_hashes_sha256.($source.Name)-ne$source.Value) { $differences+=$source.Name }
 }
 $declared=@($manifest.current_validated_benchmark.source_differences_since_measured_run)
 if (@(Compare-Object $differences $declared).Count) { throw 'Benchmark source differences are incompletely disclosed' }
 $validation=ReadReport 'v2-validation.json'
 if ($validation.latest_scene_test-ne'pass'-or$validation.latest_benchmark_status-notin@('pass','blocked_by_windows_application_control')) { throw 'Final verification status missing' }
 Write-Output "PASS: $($expected.Count) file hashes, manifest and source-only archive paths"
 Write-Output 'PASS V2 provenance, disclosed source differences, four scales, measured correctness, uploads and final verification status'
}finally{$zip.Dispose()}
