param([string]$PythonPath)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (!$PythonPath) { $PythonPath = & py -3 -c 'import sys; print(sys.executable)' }
$python = (Resolve-Path -LiteralPath $PythonPath).Path
$bundleRoot = Join-Path $projectRoot 'dist\zink'
$bundle = Get-Content -LiteralPath (Join-Path $bundleRoot 'bundle.json') -Raw | ConvertFrom-Json
foreach ($name in 'opengl32.dll','gallium_wgl.dll') {
    if ((Get-FileHash -LiteralPath (Join-Path $bundleRoot $name)).Hash -ne $bundle.hashes.$name) {
        throw "Mesa bundle changed since its manifest was generated. Review $name before testing."
    }
}
$probe = Join-Path $projectRoot 'build-native\Release\wgl-zink-api.exe'
if (!(Test-Path -LiteralPath $probe)) { throw 'Build the wgl-zink-api target first.' }
$identifier = [DateTime]::Now.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$output = Join-Path $projectRoot "dist\api-checks\$identifier"
[IO.Directory]::CreateDirectory($output) | Out-Null
$previousEnvironment = @{}
foreach ($key in 'GALLIUM_DRIVER','VULKANIZER_EVIDENCE_FILE','MESA_LOG_FILE') {
    $previousEnvironment[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
}
try {
    $env:GALLIUM_DRIVER = 'zink'
    $env:VULKANIZER_EVIDENCE_FILE = Join-Path $output 'mesa-vulkan.jsonl'
    $env:MESA_LOG_FILE = Join-Path $output 'mesa.log'
    $ErrorActionPreference = 'Continue'
    & $probe (Join-Path $bundleRoot 'opengl32.dll') 2>&1 | ForEach-Object { "$_" } | Set-Content (Join-Path $output 'probe.log')
    $probeExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($probeExit -ne 0) {
        Get-Content -LiteralPath (Join-Path $output 'probe.log') -Tail 4
        throw "WGL API check failed. Evidence folder is $output"
    }
    & $python (Join-Path $PSScriptRoot 'verify-gd-vulkan.py') --wgl-probe-log (Join-Path $output 'probe.log') --mesa-events $env:VULKANIZER_EVIDENCE_FILE
    if ($LASTEXITCODE -ne 0) { throw "WGL/Vulkan evidence check failed. Evidence folder is $output" }
    Write-Output "WGL API evidence saved in $output"
} finally {
    foreach ($key in $previousEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($key, $previousEnvironment[$key], 'Process')
    }
}
