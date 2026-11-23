param(
    [string]$PythonPath,
    [ValidateSet('move','rotation_pivot','ordered_move_rotation')][string]$ScaleCase,
    [int]$ScaleObjects = 0,
    [ValidateSet('explicit_fence','implicit_readback')][string]$ScaleSync = 'explicit_fence',
    [ValidateSet('reused','reallocated')][string]$ScaleBuffers = 'reused'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (!$PythonPath) {
    $PythonPath = Join-Path $projectRoot '.tools\mesa-venv\Scripts\python.exe'
}
$python = (Resolve-Path -LiteralPath $PythonPath).Path
$bundleRoot = Join-Path $projectRoot 'dist\zink'
$bundle = Get-Content -LiteralPath (Join-Path $bundleRoot 'bundle.json') -Raw | ConvertFrom-Json
$instrumentation = Get-Content -LiteralPath (Join-Path $bundleRoot 'source-instrumentation.json') -Raw | ConvertFrom-Json
if (!$bundle.instrumented -or $bundle.mesa_version -ne '26.2.6' -or
    $instrumentation.compute_dispatch_sites -ne 2) {
    throw 'Build the instrumented Mesa 26.2.6 Zink bundle with both Vulkan compute dispatch sites counted.'
}
foreach ($name in 'opengl32.dll','gallium_wgl.dll') {
    if ((Get-FileHash -LiteralPath (Join-Path $bundleRoot $name)).Hash -ne $bundle.hashes.$name) {
        throw "Mesa bundle hash does not match its manifest: $name"
    }
}
$probe = Join-Path $projectRoot 'build-trigger-poc\trigger-gpu-poc.exe'
if (!(Test-Path -LiteralPath $probe)) { throw 'Build the trigger-gpu-poc CMake target first.' }
$scaleOnly = ![string]::IsNullOrWhiteSpace($ScaleCase) -or $ScaleObjects -gt 0
if ($scaleOnly -and ([string]::IsNullOrWhiteSpace($ScaleCase) -or $ScaleObjects -le 0)) {
    throw 'A filtered run requires both -ScaleCase and a positive -ScaleObjects value.'
}
$identifier = [DateTime]::Now.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$output = Join-Path $projectRoot "dist\trigger-gpu-poc\$identifier"
[IO.Directory]::CreateDirectory($output) | Out-Null
$previousEnvironment = @{}
foreach ($key in 'GALLIUM_DRIVER','VULKANIZER_EVIDENCE_FILE','VULKANIZER_EVIDENCE_EVERY_PRESENT','MESA_LOG_FILE') {
    $previousEnvironment[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
}
try {
    $env:GALLIUM_DRIVER = 'zink'
    $env:VULKANIZER_EVIDENCE_FILE = Join-Path $output 'mesa-vulkan.jsonl'
    $env:VULKANIZER_EVIDENCE_EVERY_PRESENT = '1'
    $env:MESA_LOG_FILE = Join-Path $output 'mesa.log'
    $stdoutPath = Join-Path $output 'probe.stdout.log'
    $stderrPath = Join-Path $output 'probe.stderr.log'
    $probeLog = Join-Path $output 'probe.log'
    $providerArgument = '"' + (Join-Path $bundleRoot 'opengl32.dll') + '"'
    $probeArguments = @($providerArgument)
    if ($scaleOnly) {
        $probeArguments += @('--scale-only', $ScaleCase, [string]$ScaleObjects, $ScaleSync, $ScaleBuffers)
    }
    $probeProcess = Start-Process -FilePath $probe -ArgumentList $probeArguments `
        -WorkingDirectory $projectRoot -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    if (!$probeProcess.WaitForExit(120000)) {
        Stop-Process -Id $probeProcess.Id -Force -ErrorAction SilentlyContinue
        $probeProcess.WaitForExit()
        $partial = (Get-Content -LiteralPath $stdoutPath -Raw -ErrorAction SilentlyContinue) +
            [Environment]::NewLine +
            (Get-Content -LiteralPath $stderrPath -Raw -ErrorAction SilentlyContinue)
        [IO.File]::WriteAllText($probeLog, $partial, [Text.UTF8Encoding]::new($false))
        throw "Trigger GPU PoC exceeded the 120 s safety timeout. Evidence folder: $output"
    }
    $probeProcess.Refresh()
    $probeExit = $probeProcess.ExitCode
    $combined = (Get-Content -LiteralPath $stdoutPath -Raw -ErrorAction SilentlyContinue) +
        [Environment]::NewLine +
        (Get-Content -LiteralPath $stderrPath -Raw -ErrorAction SilentlyContinue)
    [IO.File]::WriteAllText($probeLog, $combined, [Text.UTF8Encoding]::new($false))
    if ($probeExit -ne 0) {
        Get-Content -LiteralPath $probeLog -Tail 6
        throw "Trigger GPU PoC exited with code $probeExit. Evidence folder: $output"
    }
    $verifier = Join-Path $PSScriptRoot 'verify-trigger-gpu-poc.py'
    if ($scaleOnly) {
        & $python $verifier --probe-log $probeLog --mesa-events $env:VULKANIZER_EVIDENCE_FILE --allow-partial
    } else {
        & $python $verifier --probe-log $probeLog --mesa-events $env:VULKANIZER_EVIDENCE_FILE
    }
    if ($LASTEXITCODE -ne 0) { throw "Trigger GPU evidence failed. Evidence folder: $output" }
    Write-Output "PoC evidence saved in $output"
} finally {
    foreach ($key in $previousEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($key, $previousEnvironment[$key], 'Process')
    }
}
