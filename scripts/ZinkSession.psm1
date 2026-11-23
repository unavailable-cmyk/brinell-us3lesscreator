Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$stateName = '.vulkanizer-zink-session.json'
$allowedNames = @('opengl32.dll', 'gallium_wgl.dll')

function Write-SessionJson($Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
}

function Assert-SettingsPath($SettingsPath) {
    $path = [IO.Path]::GetFullPath($SettingsPath)
    if ([IO.Path]::GetFileName($path) -ne 'settings.json' -or
        [IO.Path]::GetFileName([IO.Path]::GetDirectoryName($path)) -ne 'us3lesscreator.vulkanizer') {
        throw 'Only Vulkanizer settings.json may be changed.'
    }
    return $path
}

function Install-ZinkSession {
    param([Parameter(Mandatory)][string]$GameDirectory,
          [Parameter(Mandatory)][string]$BundleDirectory,
          [Parameter(Mandatory)][string]$SettingsPath)
    $gameRoot = (Resolve-Path -LiteralPath $GameDirectory).Path
    $bundleRoot = (Resolve-Path -LiteralPath $BundleDirectory).Path
    $settingsFile = Assert-SettingsPath $SettingsPath
    $stateFile = Join-Path $gameRoot $stateName
    if (Test-Path -LiteralPath $stateFile) { throw 'A prior Zink session needs recovery. Run run-gd.ps1 -RestoreOnly with the same paths.' }
    $bundle = Get-Content -LiteralPath (Join-Path $bundleRoot 'bundle.json') -Raw | ConvertFrom-Json
    if ($bundle.mesa_version -ne '26.2.6' -or $bundle.instrumented -isnot [bool]) { throw 'Expected a Mesa 26.2.6 bundle with an instrumentation flag.' }
    $files = @()
    foreach ($name in $allowedNames) {
        $source = Join-Path $bundleRoot $name
        $destination = Join-Path $gameRoot $name
        if (Test-Path -LiteralPath $destination) { throw "Existing $name was left untouched; no DLLs were installed." }
        $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        if ($hash -ne $bundle.hashes.$name) { throw "Bundle hash mismatch: $name" }
        $files += @{ name = $name; sha256 = $hash }
    }
    $original = if (Test-Path -LiteralPath $settingsFile) {
        Get-Content -LiteralPath $settingsFile -Raw | ConvertFrom-Json
    } else { [pscustomobject]@{} }
    $values = @{}
    # Zink selection owns only the backend switch. Diagnostics are a separate
    # user preference and must not be enabled as a side effect of launching.
    foreach ($key in 'experimental-renderer') {
        $property = $original.PSObject.Properties[$key]
        $values[$key] = @{ existed = $null -ne $property; value = if ($property) { $property.Value } else { $null } }
    }
    $state = @{ schema = 1; owner = 'Vulkanizer'; game_directory = $gameRoot;
        settings_path = $settingsFile; settings_original = $values; files = $files }
    # Write-ahead journal: recovery can remove only our exact hashes after interruption.
    Write-SessionJson $stateFile $state
    try {
        foreach ($entry in $files) {
            # File.Copy(overwrite:false) also closes the preflight/create race.
            [IO.File]::Copy((Join-Path $bundleRoot $entry.name), (Join-Path $gameRoot $entry.name), $false)
        }
        foreach ($key in 'experimental-renderer') {
            $original | Add-Member -MemberType NoteProperty -Name $key -Value $true -Force
        }
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($settingsFile)) | Out-Null
        Write-SessionJson $settingsFile $original
    } catch {
        Restore-ZinkSession -GameDirectory $gameRoot -SettingsPath $settingsFile
        throw
    }
    return $state
}

function Restore-ZinkSession {
    param([Parameter(Mandatory)][string]$GameDirectory,
          [Parameter(Mandatory)][string]$SettingsPath)
    $gameRoot = (Resolve-Path -LiteralPath $GameDirectory).Path
    $settingsFile = Assert-SettingsPath $SettingsPath
    $stateFile = Join-Path $gameRoot $stateName
    if (!(Test-Path -LiteralPath $stateFile)) { return }
    $state = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
    if ($state.schema -ne 1 -or $state.owner -ne 'Vulkanizer' -or
        $state.game_directory -ne $gameRoot -or $state.settings_path -ne $settingsFile) {
        throw 'Invalid recovery journal; files were left untouched.'
    }
    $seen = @()
    foreach ($entry in $state.files) {
        if ($entry.name -notin $allowedNames -or $entry.name -in $seen) { throw 'Invalid recovery filename.' }
        $seen += $entry.name
        $path = Join-Path $gameRoot $entry.name
        if ((Test-Path -LiteralPath $path) -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) {
            throw "Changed file $($entry.name) was left untouched. Manual review is needed before recovery."
        }
    }
    # Preflight every hash before removing any file. No recursive deletes.
    foreach ($entry in $state.files) {
        $path = Join-Path $gameRoot $entry.name
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
    }
    $settings = if (Test-Path -LiteralPath $settingsFile) {
        Get-Content -LiteralPath $settingsFile -Raw | ConvertFrom-Json
    } else { [pscustomobject]@{} }
    # New journals omit frame-diagnostics because this launcher no longer owns
    # that preference. Restore it only for older schema-1 journals that saved
    # it, since those sessions forcibly enabled it before this fix.
    if ($state.settings_original.PSObject.Properties['frame-diagnostics']) {
        $key = 'frame-diagnostics'
        $saved = $state.settings_original.$key
        if ($saved.existed) { $settings | Add-Member -MemberType NoteProperty -Name $key -Value $saved.value -Force }
        else { $settings.PSObject.Properties.Remove($key) }
    }
    # The launcher never owns diagnostics in new sessions, so preserve any
    # preference value the user has now.
    foreach ($key in 'experimental-renderer') {
        $saved = $state.settings_original.$key
        if ($saved.existed) { $settings | Add-Member -MemberType NoteProperty -Name $key -Value $saved.value -Force }
        else { $settings.PSObject.Properties.Remove($key) }
    }
    Write-SessionJson $settingsFile $settings
    Remove-Item -LiteralPath $stateFile
}

Export-ModuleMember -Function Install-ZinkSession, Restore-ZinkSession
