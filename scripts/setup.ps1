$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$sdkPath = Join-Path $projectRoot '.tools\geode-v6.0.0-alpha.1'
$cliPath = Join-Path $projectRoot '.tools\cli'
New-Item -ItemType Directory -Force (Join-Path $projectRoot '.tools') | Out-Null
if (!(Test-Path "$cliPath\geode.exe")) {
    $archive = Join-Path $projectRoot '.tools\geode-cli.zip'
    Invoke-WebRequest 'https://github.com/geode-sdk/cli/releases/download/v4.0.0/geode-cli-v4.0.0-win.zip' -OutFile $archive
    Expand-Archive -LiteralPath $archive -DestinationPath $cliPath
}
$cli = Join-Path $cliPath 'geode.exe'
if ((& $cli --version).Trim() -ne 'geode 4.0.0') { throw 'Expected project-local Geode CLI 4.0.0; existing executable was left unchanged.' }
if (!(Test-Path -LiteralPath $sdkPath)) {
    $ErrorActionPreference = 'Continue'
    & git clone --depth 1 --branch v6.0.0-alpha.1 https://github.com/geode-sdk/geode.git $sdkPath
    $cloneExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($cloneExit -ne 0) { throw 'SDK clone failed.' }
}
if ((Get-Content "$sdkPath\VERSION" -Raw).Trim() -ne '6.0.0-alpha.1') { throw 'Existing project-local SDK is the wrong version; left unchanged.' }
$revision = & git -C $sdkPath rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $revision -ne '101c9230fd6b39556faa33b84ad05b11aa7aa818') { throw 'Existing SDK is not the pinned official release; left unchanged.' }
$previousSdk = $env:GEODE_SDK
try {
    # Process-local selection. No sdk set-path/install: those change global state.
    $env:GEODE_SDK = $sdkPath
    & $cli sdk version
    if (!(Test-Path "$sdkPath\bin\6.0.0\Geode.lib")) {
        & $cli sdk install-binaries --version v6.0.0-alpha.1 --platform windows
        if ($LASTEXITCODE -ne 0) { throw 'Matching SDK library installation failed.' }
    }
}
finally { $env:GEODE_SDK = $previousSdk }
