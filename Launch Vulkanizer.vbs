Option Explicit
Dim shell, fso, root, scriptPath, command, exitCode, proc, env, logDir, logPath, logFile, outText, errText
Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
root = fso.GetParentFolderName(WScript.ScriptFullName)
scriptPath = fso.BuildPath(root, "scripts\VulkanizerLauncher.ps1")
If Not fso.FileExists(scriptPath) Then
    MsgBox "VulkanizerLauncher.ps1 is missing. Keep the launcher folder structure intact.", vbCritical, "Vulkanizer"
    WScript.Quit 1
End If
command = "powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -WindowStyle Hidden -File """ & scriptPath & """"
Set env = shell.Environment("PROCESS")
logDir = fso.BuildPath(env("LOCALAPPDATA"), "Vulkanizer\launcher-logs")
If Not fso.FolderExists(fso.BuildPath(env("LOCALAPPDATA"), "Vulkanizer")) Then
    fso.CreateFolder fso.BuildPath(env("LOCALAPPDATA"), "Vulkanizer")
End If
If Not fso.FolderExists(logDir) Then fso.CreateFolder logDir
logPath = fso.BuildPath(logDir, "launcher-host.log")
Set proc = shell.Exec(command)
Do While proc.Status = 0
    WScript.Sleep 200
Loop
outText = proc.StdOut.ReadAll
errText = proc.StdErr.ReadAll
exitCode = proc.ExitCode
Set logFile = fso.OpenTextFile(logPath, 8, True, -1)
logFile.WriteLine Now & " | PowerShell launcher exited with code " & exitCode
If Len(outText) > 0 Then logFile.WriteLine "STDOUT: " & outText
If Len(errText) > 0 Then logFile.WriteLine "STDERR: " & errText
logFile.Close
If exitCode <> 0 And exitCode <> -1073741510 Then
    MsgBox "The launcher closed with an error (code " & exitCode & "). No game session was started. Details were saved to:" & vbCrLf & logPath, vbExclamation, "Vulkanizer launcher"
End If
