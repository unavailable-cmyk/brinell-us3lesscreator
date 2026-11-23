param([ValidateSet('Build','Install','Restore','Launch','Benchmark','Compare')][string]$Action='Build',
 [ValidateSet('CPU','GPU')][string]$Evaluator='GPU',[string]$OutputDirectory='build/diagnostic-reports/stream-reset',
 [string[]]$RunDirectories,[switch]$Diagnostics,[switch]$Group575,[string]$PackagePath)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$installed='C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash\geode\mods\us3lesscreator.vulkanizer.geode'
Push-Location $root
try {
 switch($Action){
  Build {& "$PSScriptRoot/build.ps1"}
  Install {
   if(Get-Process GeometryDash -ErrorAction SilentlyContinue){throw 'Close GD first'}
   $source=if($PackagePath){$PackagePath}else{'build/us3lesscreator.vulkanizer.geode'}
   Copy-Item -LiteralPath $source -Destination $installed -Force
  }
  Restore {
   if(Get-Process GeometryDash -ErrorAction SilentlyContinue){throw 'Close GD first'}
   $source=if($PackagePath){$PackagePath}else{'build/diagnostic-reports/before-group575.geode'}
   Copy-Item -LiteralPath $source -Destination $installed -Force
  }
  Launch {$env:VULKANIZER_VISUAL_CPU_ONLY=$(if($Evaluator-eq'CPU'){'1'}else{$null}); & "$PSScriptRoot/run-gd.ps1" -Backend Zink -MesaDirectory "$root/dist/zink-clean"}
  Benchmark {& "$PSScriptRoot/benchmark-transform-matrix.ps1" -OutputDirectory $OutputDirectory -Modes batching -DurationSeconds 35 -CpuOnly:($Evaluator-eq'CPU') -TransformDiagnostics:$Diagnostics -Group575:$Group575}
  Compare {New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null;& "$PSScriptRoot/compare-stream.ps1" -RunDirectories $RunDirectories -ReportFile "$OutputDirectory/comparison.json"}
 }
} finally {Pop-Location}
