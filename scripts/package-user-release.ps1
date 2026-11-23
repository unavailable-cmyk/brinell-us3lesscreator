param(
    [string]$PackagePath = (Join-Path $PSScriptRoot '..\build\us3lesscreator.vulkanizer.geode'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\dist\Vulkanizer-Windows-x64')
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$package = (Resolve-Path -LiteralPath $PackagePath).Path
$zinkSource = Join-Path $projectRoot 'dist\zink'
$zinkCleanSource = Join-Path $projectRoot 'dist\zink-clean'
$presentMonSource = Join-Path $projectRoot 'dist\tools\PresentMon-2.6.0-x64.exe'
$required = @(
    (Join-Path $zinkSource 'bundle.json'),
    (Join-Path $zinkSource 'opengl32.dll'),
    (Join-Path $zinkSource 'gallium_wgl.dll'),
    (Join-Path $zinkCleanSource 'bundle.json'),
    (Join-Path $zinkCleanSource 'opengl32.dll'),
    (Join-Path $zinkCleanSource 'gallium_wgl.dll'),
    $presentMonSource,
    (Join-Path $PSScriptRoot 'VulkanizerLauncher.ps1'),
    (Join-Path $PSScriptRoot 'run-gd.ps1'),
    (Join-Path $PSScriptRoot 'capture-cpu-profile.ps1'),
    (Join-Path $PSScriptRoot 'VulkanizerCpu.wprp'),
    (Join-Path $PSScriptRoot 'ZinkSession.psm1'),
    (Join-Path $projectRoot 'Launch Vulkanizer.vbs'),
    (Join-Path $projectRoot 'docs\launcher.md')
)
foreach ($path in $required) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required release file is missing: $path" }
}

$target = [IO.Path]::GetFullPath($OutputDirectory)
$archive = $target + '.zip'
if (Test-Path -LiteralPath $target) { throw "Output already exists; choose a new OutputDirectory to avoid overwriting it: $target" }
if (Test-Path -LiteralPath $archive) { throw "Archive already exists; choose a new OutputDirectory to avoid overwriting it: $archive" }
[IO.Directory]::CreateDirectory($target) | Out-Null
try {
    $scriptsTarget = Join-Path $target 'scripts'
    [IO.Directory]::CreateDirectory($scriptsTarget) | Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $target 'dist')) | Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $target 'dist\tools')) | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot 'Launch Vulkanizer.vbs') -Destination $target
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\launcher.md') -Destination (Join-Path $target 'README-Launcher.md')
    foreach ($name in 'VulkanizerLauncher.ps1', 'run-gd.ps1', 'capture-cpu-profile.ps1', 'VulkanizerCpu.wprp', 'ZinkSession.psm1') {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $scriptsTarget
    }
    Copy-Item -LiteralPath $zinkSource -Destination (Join-Path $target 'dist') -Recurse
    Copy-Item -LiteralPath $zinkCleanSource -Destination (Join-Path $target 'dist') -Recurse
    Copy-Item -LiteralPath $presentMonSource -Destination (Join-Path $target 'dist\tools')
    Copy-Item -LiteralPath $package -Destination $target
    Compress-Archive -Path (Join-Path $target '*') -DestinationPath $archive -CompressionLevel Optimal
    Write-Output "User release staged at: $target"
    Write-Output "User release archive created at: $archive"
    Write-Output 'The archive contains the Geode mod, click-through launcher, instrumented and clean Zink bundles, and PresentMon.'
} catch {
    throw "Could not finish staging the user release at '$target'. The partial folder was left in place for inspection; it was not deleted. $($_.Exception.Message)"
}
