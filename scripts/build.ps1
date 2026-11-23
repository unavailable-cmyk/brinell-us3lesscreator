param([string]$Configuration='RelWithDebInfo')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
 if(!(Test-Path 'build/CMakeCache.txt')){throw 'Canonical configured build missing; run setup explicitly.'}
 $tracking=Join-Path $root 'build/incremental-tracking/'
 New-Item -ItemType Directory -Path $tracking -Force|Out-Null
 $ErrorActionPreference='Continue'
 & cmake --build build --target Vulkanizer --config $Configuration -- /p:BuildProjectReferences=false /p:TrackFileAccess=true "/p:TrackerLogDirectory=$tracking" *> build/dev-build.log
 $compileExit=$LASTEXITCODE
 $ErrorActionPreference='Stop'
 if($compileExit){Get-Content build/dev-build.log -Tail 12;throw 'Compilation failed; see build/dev-build.log'}
 & cmake --build build --target Vulkanizer_PACKAGE --config $Configuration -- /p:BuildProjectReferences=false /p:TrackFileAccess=true *> build/dev-package.log
 if($LASTEXITCODE){throw 'Packaging failed; see build/dev-package.log'}
 Write-Output 'Package: build/us3lesscreator.vulkanizer.geode; logs: build/dev-build.log, build/dev-package.log'
} finally {Pop-Location}
