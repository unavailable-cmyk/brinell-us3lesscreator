$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '..\scripts\ZinkSession.psm1') -Force
$root = Join-Path $PSScriptRoot ('..\.tools\session-test-' + [Guid]::NewGuid().ToString('N'))
$gameRoot = Join-Path $root 'game'
$bundleRoot = Join-Path $root 'bundle'
$settings = Join-Path $root 'us3lesscreator.vulkanizer\settings.json'
New-Item -ItemType Directory -Force $gameRoot,$bundleRoot,(Split-Path $settings) | Out-Null
function Assert($Condition, $Message) { if (!$Condition) { throw $Message } }
function Assert-Throws([scriptblock]$Action) {
    $didThrow = $false
    try { & $Action | Out-Null } catch { $didThrow = $true }
    Assert $didThrow 'Expected operation to refuse unsafe state.'
}
# Non-executable text fixtures; no game, graphics DLL, or GPU is loaded here.
$hashes = @{}
foreach ($name in 'opengl32.dll','gallium_wgl.dll') {
    Set-Content -LiteralPath (Join-Path $bundleRoot $name) -Value "SYNTHETIC fixture $name"
    $hashes[$name] = (Get-FileHash -LiteralPath (Join-Path $bundleRoot $name)).Hash
}
@{ mesa_version='26.2.6'; instrumented=$true; hashes=$hashes } | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $bundleRoot 'bundle.json')
@{ 'experimental-renderer'=$false; 'frame-diagnostics'=$false; unrelated=99 } | ConvertTo-Json | Set-Content -LiteralPath $settings
$arguments = @{ GameDirectory=$gameRoot; BundleDirectory=$bundleRoot; SettingsPath=$settings }
Install-ZinkSession @arguments | Out-Null
$active = Get-Content -LiteralPath $settings -Raw | ConvertFrom-Json
Assert $active.'experimental-renderer' 'Experimental session flag was not enabled.'
Assert (!$active.'frame-diagnostics') 'Starting Zink changed the diagnostics preference.'
$active.unrelated = 100
$active | ConvertTo-Json | Set-Content -LiteralPath $settings
Restore-ZinkSession -GameDirectory $gameRoot -SettingsPath $settings
$restored = Get-Content -LiteralPath $settings -Raw | ConvertFrom-Json
Assert (!$restored.'experimental-renderer' -and !$restored.'frame-diagnostics' -and $restored.unrelated -eq 100) 'Settings restoration lost defaults, diagnostics preference, or unrelated edits.'
Assert (!(Test-Path (Join-Path $gameRoot 'opengl32.dll'))) 'Managed DLL was not removed.'

# Clean performance bundles are valid too; they intentionally omit Vulkan event instrumentation.
$cleanBundle = @{ mesa_version='26.2.6'; instrumented=$false; hashes=$hashes }
$cleanBundle | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $bundleRoot 'bundle.json')
Install-ZinkSession @arguments | Out-Null
$duringCleanSession = Get-Content -LiteralPath $settings -Raw | ConvertFrom-Json
$duringCleanSession.'frame-diagnostics' = $true
$duringCleanSession | ConvertTo-Json | Set-Content -LiteralPath $settings
Restore-ZinkSession -GameDirectory $gameRoot -SettingsPath $settings
$afterCleanSession = Get-Content -LiteralPath $settings -Raw | ConvertFrom-Json
Assert ($afterCleanSession.'frame-diagnostics') 'Restore overwrote a diagnostics preference changed during the session.'
$cleanBundle.instrumented = $true
$cleanBundle | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $bundleRoot 'bundle.json')

Set-Content -LiteralPath (Join-Path $gameRoot 'opengl32.dll') -Value 'EXISTING USER FILE'
Assert-Throws { Install-ZinkSession @arguments }
Assert ((Get-Content -LiteralPath (Join-Path $gameRoot 'opengl32.dll') -Raw).Trim() -eq 'EXISTING USER FILE') 'Pre-existing file was changed.'
Remove-Item -LiteralPath (Join-Path $gameRoot 'opengl32.dll')

Install-ZinkSession @arguments | Out-Null
Set-Content -LiteralPath (Join-Path $gameRoot 'gallium_wgl.dll') -Value 'CHANGED BY ANOTHER ACTOR'
Assert-Throws { Restore-ZinkSession -GameDirectory $gameRoot -SettingsPath $settings }
Assert (Test-Path (Join-Path $gameRoot 'opengl32.dll')) 'Restore deleted files before preflighting every hash.'
Copy-Item -LiteralPath (Join-Path $bundleRoot 'gallium_wgl.dll') -Destination (Join-Path $gameRoot 'gallium_wgl.dll') -Force
Restore-ZinkSession -GameDirectory $gameRoot -SettingsPath $settings

Install-ZinkSession @arguments | Out-Null
$journal = Join-Path $gameRoot '.vulkanizer-zink-session.json'
$originalJournal = Get-Content -LiteralPath $journal -Raw
$badJournal = $originalJournal | ConvertFrom-Json
$badJournal.files[0].name = '..\escape.dll'
$badJournal | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $journal
Assert-Throws { Restore-ZinkSession -GameDirectory $gameRoot -SettingsPath $settings }
Set-Content -LiteralPath $journal -Value $originalJournal
Restore-ZinkSession -GameDirectory $gameRoot -SettingsPath $settings

# Recovery from a pre-fix journal must undo its old forced diagnostics toggle.
$legacySettings = Get-Content -LiteralPath $settings -Raw | ConvertFrom-Json
$legacySettings.'frame-diagnostics' = $true
$legacySettings | ConvertTo-Json | Set-Content -LiteralPath $settings
Install-ZinkSession @arguments | Out-Null
$legacyJournal = Get-Content -LiteralPath $journal -Raw | ConvertFrom-Json
$legacyJournal.settings_original | Add-Member -MemberType NoteProperty -Name 'frame-diagnostics' -Value @{ existed=$true; value=$false } -Force
$legacyJournal | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $journal
Restore-ZinkSession -GameDirectory $gameRoot -SettingsPath $settings
$legacyRestored = Get-Content -LiteralPath $settings -Raw | ConvertFrom-Json
Assert (!$legacyRestored.'frame-diagnostics') 'Recovery did not undo a prior launcher-forced diagnostics toggle.'
Write-Output 'Synthetic session checks passed: rollback, default restoration, existing-file protection, tamper protection, traversal rejection.'
