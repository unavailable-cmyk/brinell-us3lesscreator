param(
    [string]$PythonPath,
    [switch]$BuildOnly,
    [switch]$Uninstrumented,
    [int]$Jobs = 6
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (!$PythonPath) {
    $PythonPath = & py -3 -c 'import sys; print(sys.executable)'
    if ($LASTEXITCODE -ne 0) { throw 'Unable to locate Python through py -3.' }
}
$python = (Resolve-Path -LiteralPath $PythonPath).Path
$source = Join-Path $projectRoot '.tools\mesa-26.2.6'
$variant = 'instrumented'
if ($Uninstrumented) {
    $source = & $python (Join-Path $PSScriptRoot 'prepare-mesa-pristine.py')
    if ($LASTEXITCODE -ne 0) { throw 'Unable to prepare clean Mesa source.' }
    $variant = 'clean'
}
$buildName = "build-zink-$variant"
if ($BuildOnly) { $buildName += '-buildonly' }
$build = Join-Path $projectRoot $buildName
$pythonDeps = Join-Path $projectRoot '.tools\mesa-python-build-deps'
$venvSitePackages = Join-Path $projectRoot '.tools\mesa-venv\Lib\site-packages'
if (!(Test-Path -LiteralPath (Join-Path $pythonDeps 'yaml\__init__.py'))) {
    [IO.Directory]::CreateDirectory($pythonDeps) | Out-Null
    foreach ($package in @('yaml', '_yaml')) {
        Copy-Item -LiteralPath (Join-Path $venvSitePackages $package) -Destination $pythonDeps -Recurse -Force
    }
}
$previousPath = $env:PATH
$previousPythonPath = $env:PYTHONPATH
Push-Location $projectRoot
try {
    if (!(Test-Path -LiteralPath "$source\VERSION")) { throw 'The checksum-verified Mesa 26.2.6 source archive is required in .tools.' }
    if ((Get-Content "$source\VERSION" -Raw).Trim() -ne '26.2.6') { throw 'Wrong Mesa source version.' }
    if (!$Uninstrumented) {
        & $python (Join-Path $PSScriptRoot 'instrument-mesa.py') $source
        if ($LASTEXITCODE -ne 0) { throw 'Mesa evidence instrumentation failed.' }
    }
    $env:PATH = "$(Join-Path $projectRoot '.tools\winflexbison\bin\Release');$(Split-Path $python -Parent);$previousPath"
    $env:PYTHONPATH = "$pythonDeps;$previousPythonPath"
    & $python -m mesonbuild.mesonmain --version
    if ($LASTEXITCODE -ne 0) { throw 'Meson is unavailable in the selected Python installation.' }
    $options = @('--vsenv', '--buildtype=release', '--wrap-mode=nofallback',
        '-Dgallium-drivers=zink', '-Dgallium-wgl-dll-name=gallium_wgl', '-Dvulkan-drivers=[]', '-Dplatforms=windows',
        '-Dopengl=true', '-Dgles1=disabled', '-Dgles2=disabled', '-Dglx=disabled', '-Degl=disabled', '-Dgbm=disabled',
        '-Dllvm=disabled', '-Ddraw-use-llvm=false', '-Dgallium-rusticl=false', '-Dgallium-va=disabled',
        '-Dgallium-mediafoundation=disabled', '-Dmicrosoft-clc=disabled', '-Dvideo-codecs=[]',
        '-Dzlib=disabled', '-Dzstd=disabled', '-Dshader-cache=disabled', '-Dexpat=disabled',
        '-Dxmlconfig=disabled', '-Dlibunwind=disabled', '-Dbuild-tests=false')
    $ErrorActionPreference = 'Continue'
    $setupArgs = @('setup', $build, $source)
    if ($BuildOnly) {
        $setupArgs += @('--cross-file', (Join-Path $projectRoot 'integration\mesa\windows-x64-build-only.ini'))
    }
    if (Test-Path "$build\meson-private\coredata.dat") { $setupArgs += '--reconfigure' }
    & $python -m mesonbuild.mesonmain @setupArgs @options 2>&1 | ForEach-Object { "$_" } | Set-Content .tools\mesa-configure.log
    $configureExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($configureExit -ne 0 -or !(Test-Path "$build\build.ninja")) { throw 'Mesa configuration failed. Inspect .tools/mesa-configure.log.' }
    $ErrorActionPreference = 'Continue'
    & $python -m mesonbuild.mesonmain compile -C $build -j $Jobs opengl32 gallium_wgl 2>&1 | ForEach-Object { "$_" } | Set-Content .tools\mesa-build.log
    $compileExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($compileExit -ne 0) { throw 'Mesa compilation failed; inspect .tools/mesa-build.log.' }
    $destinationName = if ($Uninstrumented) { 'zink-clean' } else { 'zink' }
    $destination = Join-Path $projectRoot "dist\$destinationName"
    [IO.Directory]::CreateDirectory($destination) | Out-Null
    $outputs = @{
        'opengl32.dll' = "$build\src\gallium\targets\libgl-gdi\opengl32.dll"
        'gallium_wgl.dll' = "$build\src\gallium\targets\wgl\gallium_wgl.dll"
    }
    $hashes = @{}
    foreach ($name in $outputs.Keys) {
        Copy-Item -LiteralPath $outputs[$name] -Destination (Join-Path $destination $name)
        $hashes[$name] = (Get-FileHash -LiteralPath (Join-Path $destination $name)).Hash
    }
    Copy-Item -LiteralPath "$source\docs\license.rst" -Destination "$destination\Mesa-license.rst"
    if (!$Uninstrumented) {
        Copy-Item -LiteralPath "$source\src\gallium\drivers\zink\vulkanizer-patch.json" -Destination "$destination\source-instrumentation.json"
    }
    $manifest = @{ mesa_version = '26.2.6'; instrumented = !$Uninstrumented; shader_disk_cache = $false; target_execution_checks = !$BuildOnly;
        upstream_archive_sha256 = 'b9436b3e1a8be76fbd4823dc884f90596ab7c6ac5e1867a305e66ffccfb31a92'; hashes = $hashes }
    [IO.File]::WriteAllText("$destination\bundle.json", ($manifest | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
    $kind = if ($Uninstrumented) { 'uninstrumented' } else { 'instrumented' }
    Write-Output "Built $kind Windows Zink bundle in $destination. GD execution and visual correctness remain unverified."
} finally {
    $env:PATH = $previousPath
    $env:PYTHONPATH = $previousPythonPath
    Pop-Location
}
