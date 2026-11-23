param(
    [string]$BuildDirectory='build-native',
    [string]$Configuration='Release',
    [string]$OpenGLProvider,
    [string]$OutputDirectory='build/diagnostic-reports'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
function ProjectPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return $Path }
    return Join-Path $root $Path
}
$build=ProjectPath $BuildDirectory
$output=ProjectPath $OutputDirectory
[IO.Directory]::CreateDirectory($output)|Out-Null
$results=@{}
$pendingReports=@{}
foreach ($kind in @('cpu','gl')) {
    $target=if ($kind-eq'cpu') {'v2-chain-benchmark'} else {'v2-gl-benchmark'}
    $executable=Join-Path $build "$Configuration/$target.exe"
    if (!(Test-Path -LiteralPath $executable)) { $executable=Join-Path $build "$target.exe" }
    if (!(Test-Path -LiteralPath $executable)) { throw "Build $target first in $build" }
    $temporary=Join-Path $output "v2-$kind-benchmark.partial.json"
    $errors=Join-Path $output "v2-$kind-benchmark.stderr.txt"
    $arguments=@()
    if ($kind-eq'gl'-and$OpenGLProvider) {
        $provider=(Resolve-Path -LiteralPath (ProjectPath $OpenGLProvider)).Path
        $arguments=@('"'+$provider+'"')
    }
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=$executable
    $start.WorkingDirectory=$root
    $start.Arguments=$arguments-join' '
    $start.UseShellExecute=$false
    $start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true
    $start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::new()
    $process.StartInfo=$start
    if (!$process.Start()) { throw "Could not start $target" }
    $stdout=$process.StandardOutput.ReadToEndAsync()
    $stderr=$process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $exitCode=$process.ExitCode
    [IO.File]::WriteAllText($temporary,$stdout.Result,[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($errors,$stderr.Result,[Text.UTF8Encoding]::new($false))
    $process.Dispose()
    if ($exitCode-ne0) {
        Get-Content -LiteralPath $errors
        throw "$target failed with exit code $exitCode. Partial output is in $temporary"
    }
    $report=Get-Content -LiteralPath $temporary -Raw|ConvertFrom-Json
    if ($report.schema-ne1-or$report.cases.Count-lt8) { throw "Incomplete $target report" }
    foreach ($case in $report.cases) {
        if ($case.routine_object_visits-ne0) { throw 'V2 routine object traversal regression' }
        if ($case.max_position_error-gt0) { throw 'V2 transform reference mismatch' }
        if ($kind-eq'gl'-and($case.image_mismatched_channels-ne0-or$case.colored_pixels-le0)) {
            throw 'V2 pixel reference mismatch or empty output'
        }
    }
    $destination=Join-Path $output "v2-$kind-benchmark.json"
    $pendingReports[$destination]=$temporary
    $results[$kind]=@{report=[IO.Path]::GetFileName($destination);executable_sha256=(Get-FileHash -LiteralPath $executable).Hash}
}
$metadata=[ordered]@{
    timestamp_utc=[DateTime]::UtcNow.ToString('o')
    configuration=$Configuration
    build_directory=$BuildDirectory
    cpu=(Get-ItemProperty 'HKLM:\HARDWARE\DESCRIPTION\System\CentralProcessor\0').ProcessorNameString.Trim()
    renderer=(Get-Content -LiteralPath (Join-Path $output 'v2-gl-benchmark.partial.json') -Raw|ConvertFrom-Json).renderer
    provider=if ($OpenGLProvider) {$OpenGLProvider} else {'System32/opengl32.dll'}
    results=$results
    scope='Controlled textured quads at 512x320, 16 warmup frames, 240 measured frames. CPU submission includes shared scheduling, palette uploads and drawing. GPU queries cover palette upload and drawing. Clear and validation readbacks occur outside timing.'
}
$sourceHashes=[ordered]@{}
$sourceFiles=@(Get-ChildItem -LiteralPath (Join-Path $root 'src/v2') -File)
$sourceFiles+=@(Get-ChildItem -LiteralPath (Join-Path $root 'tests') -File -Filter 'v2-*')
$sourceFiles+=Get-Item -LiteralPath $PSCommandPath
foreach ($file in $sourceFiles|Sort-Object FullName) {
    $relative=$file.FullName.Substring($root.Length+1).Replace('\','/')
    $sourceHashes[$relative]=(Get-FileHash -LiteralPath $file.FullName).Hash
}
$metadata.source_file_hashes_sha256=$sourceHashes
foreach ($destination in $pendingReports.Keys) {
    Move-Item -LiteralPath $pendingReports[$destination] -Destination $destination -Force
}
$metadata|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $output 'v2-benchmark-run.json') -Encoding UTF8
Write-Output "PASS V2 benchmarks saved to $output"
