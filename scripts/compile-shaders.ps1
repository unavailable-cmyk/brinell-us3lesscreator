param([Parameter(Mandatory)][string]$Glslang)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    & $Glslang @('-V', '--target-env', 'vulkan1.0', '--vn', 'triangleVertex', '-o', 'src/backend/generated/TriangleVertex.hpp', 'shaders/triangle.vert')
    if ($LASTEXITCODE -ne 0) { throw 'Vertex shader compilation failed' }
    & $Glslang @('-V', '--target-env', 'vulkan1.0', '--vn', 'triangleFragment', '-o', 'src/backend/generated/TriangleFragment.hpp', 'shaders/triangle.frag')
    if ($LASTEXITCODE -ne 0) { throw 'Fragment shader compilation failed' }
}
finally { Pop-Location }
