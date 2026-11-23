param(
    [ValidateSet('OpenGL', 'Zink')][string]$LaunchBackend,
    [string]$GameExe = 'C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash\GeometryDash.exe',
    [switch]$RestoreOnly,
    [switch]$Confirmed,
    [switch]$CaptureCpuProfile,
    [switch]$CapturePresentMon,
    [switch]$UseCleanMesa
)

$ErrorActionPreference = 'Stop'
$localData = Join-Path $env:LOCALAPPDATA 'Vulkanizer'
$configPath = Join-Path $localData 'launcher.json'
$logDir = Join-Path $localData 'launcher-logs'
[IO.Directory]::CreateDirectory($logDir) | Out-Null
$logStamp = [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff')
$script:uiLogPath = Join-Path $logDir ('launcher-' + $logStamp + '.log')
$transcriptPath = Join-Path $logDir ('launcher-transcript-' + $logStamp + '.log')
$script:transcriptStarted = $false
try {
    Start-Transcript -Path $transcriptPath -ErrorAction Stop | Out-Null
    $script:transcriptStarted = $true
} catch {
    [IO.File]::AppendAllText($script:uiLogPath, "[$([DateTime]::Now.ToString('s'))] Transcript startup failed: $($_.Exception.Message)`r`n")
}

function Write-LauncherLog([string]$Message) {
    $line = "[$([DateTime]::Now.ToString('s'))] $Message"
    [IO.File]::AppendAllText($script:uiLogPath, $line + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
}

Write-LauncherLog 'Launcher started.'
Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase
Add-Type -AssemblyName System.Windows.Forms

$runnerPath = Join-Path $PSScriptRoot 'run-gd.ps1'

if (Test-Path -LiteralPath $configPath) {
    try {
        $saved = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
        if ($saved.game_exe -and (Test-Path -LiteralPath $saved.game_exe -PathType Leaf)) {
            $GameExe = [string]$saved.game_exe
        }
    } catch { Write-LauncherLog "Saved launcher settings could not be read: $($_.Exception.Message)" }
}

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Start-ElevatedAction([string]$Backend, [bool]$Recover, [bool]$CaptureCpu, [bool]$CaptureFrames, [bool]$UninstrumentedMesa, [string]$Executable) {
    $parts = @('-NoProfile', '-STA', '-ExecutionPolicy', 'Bypass', '-WindowStyle', 'Hidden', '-File', ('"' + $PSCommandPath + '"'), '-GameExe', ('"' + $Executable + '"'), '-Confirmed')
    $parts += @('-LaunchBackend', $Backend)
    if ($Recover) { $parts += '-RestoreOnly' }
    if ($CaptureCpu) { $parts += '-CaptureCpuProfile' }
    if ($CaptureFrames) { $parts += '-CapturePresentMon' }
    if ($UninstrumentedMesa) { $parts += '-UseCleanMesa' }
    Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList ($parts -join ' ') -Verb RunAs -WindowStyle Hidden | Out-Null
}

function Save-GamePath([string]$Executable) {
    [IO.Directory]::CreateDirectory($localData) | Out-Null
    @{ game_exe = $Executable } | ConvertTo-Json | Set-Content -LiteralPath $configPath -Encoding UTF8
}

function Get-SteamExecutable([string]$Executable) {
    $candidates = [Collections.Generic.List[string]]::new()
    $gameDirectory = Split-Path -Parent $Executable
    $steamRoot = $gameDirectory
    for ($i = 0; $i -lt 3; ++$i) { $steamRoot = Split-Path -Parent $steamRoot }
    if ($steamRoot) { $candidates.Add((Join-Path $steamRoot 'steam.exe')) }
    foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam') {
        try {
            $entry = Get-ItemProperty -LiteralPath $key -ErrorAction Stop
            foreach ($property in 'SteamExe', 'SteamPath', 'InstallPath') {
                $value = [string]$entry.$property
                if (!$value) { continue }
                $value = $value.Trim('"')
                if ([IO.Path]::GetExtension($value) -ieq '.exe') { $candidates.Add($value) }
                else { $candidates.Add((Join-Path $value 'steam.exe')) }
            }
        } catch { }
    }
    if (${env:ProgramFiles(x86)}) { $candidates.Add((Join-Path ${env:ProgramFiles(x86)} 'Steam\steam.exe')) }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    return $null
}

function Ensure-SteamClient([string]$Executable) {
    if (Get-Process -Name steam -ErrorAction SilentlyContinue) {
        Write-LauncherLog 'Steam client already running; keeping the normal direct game launch.'
        return
    }
    $steamExe = Get-SteamExecutable $Executable
    if (!$steamExe) {
        Write-LauncherLog 'Steam is not running and no Steam installation was found; preserving direct-launch fallback.'
        return
    }
    Write-LauncherLog "Cold-starting Steam client from: $steamExe"
    Start-Process -FilePath $steamExe -WorkingDirectory (Split-Path -Parent $steamExe) | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    do {
        if (Get-Process -Name steam -ErrorAction SilentlyContinue) { break }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    if (!(Get-Process -Name steam -ErrorAction SilentlyContinue)) { throw 'Steam did not start within 60 seconds. No Geometry Dash session was staged.' }
    Start-Sleep -Seconds 3
    Write-LauncherLog 'Steam cold-start completed; Geometry Dash will now launch normally through its Steam-aware executable.'
}

$xaml = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="Vulkanizer" Height="700" Width="670" MinHeight="700" MinWidth="670"
        WindowStartupLocation="CenterScreen" ResizeMode="CanMinimize" Background="#151923" Foreground="#F1F4F8">
  <Grid Margin="24">
    <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
    <StackPanel Grid.Row="0" Margin="0,0,0,16">
      <TextBlock Text="Vulkanizer" FontSize="28" FontWeight="Bold"/>
      <TextBlock Text="Choose how Geometry Dash starts. OpenGL is the safe default. Steam will open first if needed." Margin="0,4,0,0" Foreground="#B8C1CF"/>
    </StackPanel>
    <Grid Grid.Row="1" Margin="0,0,0,16">
      <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
      <TextBox x:Name="GamePath" Grid.Column="0" Height="32" VerticalContentAlignment="Center" Padding="8"/>
      <Button x:Name="BrowseButton" Grid.Column="1" Content="Browse…" Margin="8,0,0,0" Padding="14,4"/>
    </Grid>
    <Border Grid.Row="2" Background="#202735" CornerRadius="8" Padding="16" Margin="0,0,0,16">
      <StackPanel>
        <TextBlock Text="OpenGL" FontSize="18" FontWeight="SemiBold"/>
        <TextBlock Text="Start Geometry Dash with its normal graphics driver. Use this if the experimental route is unavailable or you want the standard setup." TextWrapping="Wrap" Margin="0,6,0,10" Foreground="#B8C1CF"/>
        <Button x:Name="OpenGLButton" Content="Launch Geometry Dash (OpenGL)" HorizontalAlignment="Left" Padding="14,8"/>
        <Separator Margin="0,16,0,14"/>
        <TextBlock Text="Experimental Vulkan through Zink" FontSize="18" FontWeight="SemiBold"/>
        <TextBlock Text="Uses Mesa Zink to translate Geometry Dash’s OpenGL calls to Vulkan. This is experimental and may be slower or fail on some systems. A Windows permission prompt may appear to stage the driver in the game folder. The launcher restores those files when the game closes." TextWrapping="Wrap" Margin="0,6,0,10" Foreground="#B8C1CF"/>
        <Button x:Name="ZinkButton" Content="Launch with Experimental Vulkan" HorizontalAlignment="Left" Padding="14,8"/>
        <CheckBox x:Name="CpuCaptureCheckBox" Content="Capture CPU profile for troubleshooting (optional; needs Windows Performance Toolkit)" Margin="0,10,0,0" Foreground="#D7DEE9"/>
        <CheckBox x:Name="FrameCaptureCheckBox" Content="Capture frame-time CSV for A/B benchmarks (PresentMon; low overhead)" Margin="0,6,0,0" Foreground="#D7DEE9"/>
        <CheckBox x:Name="CleanMesaCheckBox" Content="Performance run: use uninstrumented Zink (no Mesa API trace)" Margin="0,6,0,0" Foreground="#D7DEE9" ToolTip="Reduces measurement overhead. Vulkan API JSON evidence is not written for this run."/>
      </StackPanel>
    </Border>
    <Button x:Name="RecoverButton" Grid.Row="3" Content="Recover interrupted Zink setup…" HorizontalAlignment="Left" Padding="12,6" Visibility="Collapsed"/>
    <Grid Grid.Row="4" Margin="0,12,0,0">
      <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
      <Border Grid.Column="0" Background="#202735" CornerRadius="6" Padding="10">
        <TextBlock x:Name="StatusText" Text="Ready. Geometry Dash must be closed before starting a session." TextWrapping="Wrap" Foreground="#D7DEE9"/>
      </Border>
      <Button x:Name="LogsButton" Grid.Column="1" Content="Open logs" Margin="8,0,0,0" Padding="12,6"/>
    </Grid>
  </Grid>
</Window>
'@

$reader = [System.Xml.XmlNodeReader]::new([xml]$xaml)
$window = [System.Windows.Markup.XamlReader]::Load($reader)
$gamePathBox = $window.FindName('GamePath')
$gamePathBox.Text = $GameExe
$browseButton = $window.FindName('BrowseButton')
$openGlButton = $window.FindName('OpenGLButton')
$zinkButton = $window.FindName('ZinkButton')
$recoverButton = $window.FindName('RecoverButton')
$statusText = $window.FindName('StatusText')
$logsButton = $window.FindName('LogsButton')
$cpuCaptureCheckBox = $window.FindName('CpuCaptureCheckBox')
$cpuCaptureCheckBox.IsChecked = [bool]$CaptureCpuProfile
$frameCaptureCheckBox = $window.FindName('FrameCaptureCheckBox')
$frameCaptureCheckBox.IsChecked = [bool]$CapturePresentMon
$cleanMesaCheckBox = $window.FindName('CleanMesaCheckBox')
$cleanMesaCheckBox.IsChecked = [bool]$UseCleanMesa
$timer = [System.Windows.Threading.DispatcherTimer]::new()
$timer.Interval = [TimeSpan]::FromMilliseconds(500)
$script:activeProcess = $null
$script:stdoutPath = $null
$script:stderrPath = $null
$script:showDialog = $true

function Refresh-RecoveryButton {
    $exe = $gamePathBox.Text
    if ($exe -and (Test-Path -LiteralPath $exe -PathType Leaf)) {
        $journal = Join-Path (Split-Path -Parent $exe) '.vulkanizer-zink-session.json'
        $needsRecovery = Test-Path -LiteralPath $journal
        $recoverButton.Visibility = if ($needsRecovery) { 'Visible' } else { 'Collapsed' }
        if (!$script:activeProcess) {
            $openGlButton.IsEnabled = !$needsRecovery
            $zinkButton.IsEnabled = !$needsRecovery
        }
    } else { $recoverButton.Visibility = 'Collapsed' }
}

function Set-UiBusy([bool]$Busy) {
    $openGlButton.IsEnabled = !$Busy
    $zinkButton.IsEnabled = !$Busy
    $browseButton.IsEnabled = !$Busy
    $recoverButton.IsEnabled = !$Busy
}

function Start-Runner([string]$Backend, [bool]$Recover) {
    $exe = $gamePathBox.Text.Trim()
    $captureCpu = [bool]$cpuCaptureCheckBox.IsChecked -and !$Recover
    $captureFrames = [bool]$frameCaptureCheckBox.IsChecked -and !$Recover
    $useCleanMesa = [bool]$cleanMesaCheckBox.IsChecked -and $Backend -eq 'Zink' -and !$Recover
    if (!(Test-Path -LiteralPath $exe -PathType Leaf) -or [IO.Path]::GetFileName($exe) -ne 'GeometryDash.exe') {
        [System.Windows.MessageBox]::Show('Select the GeometryDash.exe file from your Geometry Dash installation.', 'Geometry Dash path required', 'OK', 'Information') | Out-Null
        return
    }
    if (!(Test-Path -LiteralPath $runnerPath -PathType Leaf)) {
        [System.Windows.MessageBox]::Show("The launcher helper is missing:`n$runnerPath`nKeep the launcher files together with the Vulkanizer release folder.", 'Launcher files missing', 'OK', 'Error') | Out-Null
        return
    }
    $cleanMesaDirectory = Join-Path $PSScriptRoot '..\dist\zink-clean'
    if ($useCleanMesa -and !(Test-Path -LiteralPath (Join-Path $cleanMesaDirectory 'bundle.json') -PathType Leaf)) {
        [System.Windows.MessageBox]::Show("The uninstrumented Zink bundle is missing:`n$cleanMesaDirectory", 'Performance bundle unavailable', 'OK', 'Error') | Out-Null
        return
    }
    Save-GamePath $exe
    if (Get-Process GeometryDash -ErrorAction SilentlyContinue) {
        [System.Windows.MessageBox]::Show('Close Geometry Dash first, then try again. This prevents changing the graphics provider while the game is using it.', 'Geometry Dash is running', 'OK', 'Information') | Out-Null
        return
    }
    if ($Backend -eq 'Zink' -and !$Recover -and !$Confirmed) {
        $choice = [System.Windows.MessageBox]::Show("This starts an experimental OpenGL-to-Vulkan translation path. It can be slower or unstable. Vulkanizer will stage only its bundled files in the selected game folder, then restore the saved settings and remove those files when Geometry Dash exits normally. If the PC shuts down during the session, use the recovery button here before launching again.`n`nContinue?", 'Experimental Vulkan session', 'YesNo', 'Warning')
        if ($choice -ne 'Yes') { return }
    }
    if (!$Recover) {
        try {
            $statusText.Text = 'Checking Steam…'
            $window.Dispatcher.Invoke([Action]{}, [System.Windows.Threading.DispatcherPriority]::Background)
            Ensure-SteamClient $exe
        } catch {
            Write-LauncherLog "Steam preflight failed: $($_.Exception.Message)"
            $statusText.Text = 'Steam could not be started. No Zink files were staged; open logs for details.'
            [System.Windows.MessageBox]::Show($_.Exception.Message, 'Steam could not start', 'OK', 'Warning') | Out-Null
            return
        }
    }
    if ($Recover) { Write-LauncherLog 'Recovery selected.' }
    else { Write-LauncherLog "Launch selected: backend=$Backend; cpu-profile=$captureCpu; presentmon=$captureFrames; uninstrumented-mesa=$useCleanMesa; game=$exe" }
    if (($Backend -eq 'Zink' -or $Recover) -and !(Test-IsAdministrator)) {
        try {
            Start-ElevatedAction -Backend $Backend -Recover $Recover -CaptureCpu $captureCpu -CaptureFrames $captureFrames -UninstrumentedMesa $useCleanMesa -Executable $exe
            Write-LauncherLog 'Started the elevated launcher after explicit user selection.'
            $script:showDialog = $false
            $window.Close()
        } catch {
            Write-LauncherLog "Could not start elevated launcher: $($_.Exception.Message)"
            $statusText.Text = 'Permission was not granted; no Zink files were staged.'
            [System.Windows.MessageBox]::Show('Windows permission was declined or unavailable. Nothing was changed.', 'Permission not granted', 'OK', 'Information') | Out-Null
        }
        return
    }
    $stamp = [DateTime]::Now.ToString('yyyyMMdd-HHmmss')
    $script:stdoutPath = Join-Path $logDir ("launcher-$stamp-$Backend.out.log")
    $script:stderrPath = Join-Path $logDir ("launcher-$stamp-$Backend.err.log")
    Write-LauncherLog "Runner output files: stdout=$script:stdoutPath; stderr=$script:stderrPath"
    $argumentList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-WindowStyle', 'Hidden', '-File', ('"' + $runnerPath + '"'), '-GameExe', ('"' + $exe + '"'))
    if ($Recover) { $argumentList += '-RestoreOnly' } else { $argumentList += @('-Backend', $Backend) }
    if ($captureCpu) { $argumentList += '-CaptureCpuProfile' }
    if ($captureFrames) { $argumentList += '-CapturePresentMon' }
    if ($useCleanMesa) {
        $argumentList += @('-MesaDirectory', ('"' + (Resolve-Path -LiteralPath $cleanMesaDirectory).Path + '"'))
    }
    try {
        $script:activeProcess = Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList ($argumentList -join ' ') -RedirectStandardOutput $script:stdoutPath -RedirectStandardError $script:stderrPath -WindowStyle Hidden -PassThru
        Write-LauncherLog "Runner process started: pid=$($script:activeProcess.Id)"
        Set-UiBusy $true
        $statusText.Text = if ($Recover) { 'Recovering the recorded Zink session…' } elseif ($Backend -eq 'Zink' -and $useCleanMesa) { 'Starting an uninstrumented Zink performance run…' } elseif ($Backend -eq 'Zink') { 'Starting Geometry Dash through Zink… this window will close after the game starts.' } else { 'Starting Geometry Dash with OpenGL… this window will close after the game starts.' }
        $timer.Start()
    } catch {
        Write-LauncherLog "Could not start runner: $($_.Exception.Message)"
        Set-UiBusy $false
        $statusText.Text = 'The launcher could not start the session.'
        [System.Windows.MessageBox]::Show($_.Exception.Message, 'Could not start Geometry Dash', 'OK', 'Error') | Out-Null
    }
}

$browseButton.Add_Click({
    $dialog = [System.Windows.Forms.OpenFileDialog]::new()
    $dialog.Title = 'Select GeometryDash.exe'
    $dialog.Filter = 'Geometry Dash executable (GeometryDash.exe)|GeometryDash.exe'
    $dialog.FileName = 'GeometryDash.exe'
    if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
        $gamePathBox.Text = $dialog.FileName
        Save-GamePath $dialog.FileName
        Write-LauncherLog "Game executable selected: $($dialog.FileName)"
        Refresh-RecoveryButton
    }
})
$logsButton.Add_Click({ Start-Process -FilePath explorer.exe -ArgumentList ('"' + $logDir + '"') })
$gamePathBox.Add_LostFocus({ Refresh-RecoveryButton })
$openGlButton.Add_Click({ Start-Runner -Backend 'OpenGL' -Recover $false })
$zinkButton.Add_Click({ Start-Runner -Backend 'Zink' -Recover $false })
$recoverButton.Add_Click({
    $choice = [System.Windows.MessageBox]::Show('Recovery uses the session journal to remove only the Zink files Vulkanizer recorded and restore its saved settings. Geometry Dash must be closed.', 'Recover interrupted setup', 'YesNo', 'Warning')
    if ($choice -eq 'Yes') { Start-Runner -Backend 'Zink' -Recover $true }
})
$timer.Add_Tick({
    if (!$script:activeProcess) { return }
    if (!$script:activeProcess.HasExited) {
        if (!$RestoreOnly -and (Test-Path -LiteralPath $script:stdoutPath)) {
            $launchLine = Get-Content -LiteralPath $script:stdoutPath -Tail 12 | Where-Object { $_ -match 'Session evidence:' } | Select-Object -Last 1
            if ($launchLine -match 'Session evidence:\s*(.+)$') {
                $sessionRoot = $Matches[1].Trim()
                $sessionFile = Join-Path $sessionRoot 'session.json'
                if (Test-Path -LiteralPath $sessionFile) {
                    try {
                        $session = Get-Content -LiteralPath $sessionFile -Raw | ConvertFrom-Json
                        $game = Get-Process -Id ([int]$session.pid) -ErrorAction SilentlyContinue
                        if ($game) {
                            Write-LauncherLog "Geometry Dash confirmed running (PID $($game.Id)); closing launcher window while background session cleanup continues. Evidence: $sessionRoot"
                            $statusText.Text = 'Geometry Dash is running. Background logging and cleanup will continue.'
                            $timer.Stop()
                            $window.Close()
                            return
                        }
                    } catch { Write-LauncherLog "Could not confirm Geometry Dash process: $($_.Exception.Message)" }
                }
            }
        }
        return
    }
    $timer.Stop()
    Set-UiBusy $false
    $out = if (Test-Path -LiteralPath $script:stdoutPath) { @(Get-Content -LiteralPath $script:stdoutPath -Tail 8) } else { @() }
    $err = if (Test-Path -LiteralPath $script:stderrPath) { @(Get-Content -LiteralPath $script:stderrPath -Tail 8) } else { @() }
    $route = $out | Where-Object { $_ -match 'Session evidence:' } | Select-Object -Last 1
    $sessionFolder = $null
    if ($route -match 'Session evidence:\s*(.+)$') { $sessionFolder = $Matches[1].Trim() }
    Write-LauncherLog "Runner exited: code=$($script:activeProcess.ExitCode); stdout=$script:stdoutPath; stderr=$script:stderrPath"
    if ($route) { Write-LauncherLog $route }
    if ($script:activeProcess.ExitCode -eq 0) {
        $statusText.Text = if ($sessionFolder) { "Session finished. Evidence folder: $sessionFolder" } else { "Session finished. Logs: $logDir" }
    } else {
        Write-LauncherLog ('Runner failure details: ' + ((@($err + $out) | Select-Object -Last 8) -join ' | '))
        $statusText.Text = "Session failed. Open logs for details: $logDir"
    }
    Refresh-RecoveryButton
})

Refresh-RecoveryButton
if ($LaunchBackend) { Start-Runner -Backend $LaunchBackend -Recover $RestoreOnly }
if ($script:showDialog) {
    try { [void]$window.ShowDialog() }
    finally {
        Write-LauncherLog 'Launcher window closed.'
        if ($script:transcriptStarted) { Stop-Transcript | Out-Null }
    }
} elseif ($script:transcriptStarted) { Stop-Transcript | Out-Null }
