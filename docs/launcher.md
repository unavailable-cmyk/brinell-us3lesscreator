# Vulkanizer Windows launcher

The Geode mod loads through Geode as usual when Geometry Dash starts. The separate launcher is only needed to select the experimental Zink route, because Windows must select its app-local OpenGL provider and process environment before Geometry Dash creates its graphics context.

## For users

1. Extract the complete Windows release folder to a writable location (for example, Documents). Keep its `scripts`, `dist\zink`, and `dist\zink-clean` folders beside `Launch Vulkanizer.vbs`.
2. Double-click **Launch Vulkanizer.vbs**.
3. Choose **Launch Geometry Dash (OpenGL)** for the normal route or **Launch with Experimental Vulkan** to opt into Zink. The game path can be selected with **Browse** and is remembered for the next run.
4. For Zink, approve the Windows permission prompt only after choosing the experimental option. Vulkanizer stages its bundled provider in the selected game folder, temporarily enables only **Experimental Zink session**, and restores that setting and its staged files after GD exits. **Frame diagnostics** is an independent preference and is never changed by launching Zink. For FPS comparisons, check **Performance run: use uninstrumented Zink** and **Capture frame-time CSV for A/B benchmarks**; this avoids Mesa API logging and captures PresentMon frame intervals. This mode does not write Vulkan API JSON evidence. The launcher window closes after GD starts; its hidden session process continues logging and cleanup.
5. If Windows shuts down, the game is force-ended, or cleanup is interrupted, start the launcher again and use **Recover interrupted Zink setup** before another launch.

The launcher does not replace files it did not stage. If it finds an unrecognized local `opengl32.dll` or a previous recovery journal, it refuses a new session. Use recovery only when the journal is shown. Declining the Windows permission prompt makes no changes.

If Steam is not running after a Windows restart, the launcher starts the installed Steam client first, waits for it to appear, then starts Geometry Dash normally. If Steam cannot be found, it keeps the direct-launch fallback and records that in the launcher log.

The standard OpenGL button does not request elevation. Zink and recovery request elevation only after the user chooses those actions and only when the launcher is not already elevated. The diagnostic PowerShell commands remain available for developers, but end users do not need to type them. The launcher uses process-scoped PowerShell execution policy settings; it does not change the machine or user execution policy. Turn **Frame diagnostics** off in Geode settings for performance runs; this removes Vulkanizer's Cocos phase detours entirely. The setting requires a restart.

Select **Open logs** in the launcher to open `%LOCALAPPDATA%\Vulkanizer\launcher-logs`. The host error log, UI event log/transcript, and runner stdout/stderr are kept there. Each run also saves a final `session.json` with the game and PresentMon exit status, Geode logs, and optional Mesa/Vulkan evidence and performance captures in the release folder's `dist\sessions` directory; PresentMon stdout/stderr are saved there if capture fails. The UI log records that session folder. For a short CPU sample, check **Capture CPU profile for troubleshooting** before starting Zink; the ETL and summary are written into that run's session folder. For benchmark CSV frame times, use the PresentMon checkbox; leave the CPU profile option off to keep sampling overhead low.

## Release contents

The launcher uses `scripts\VulkanizerLauncher.ps1`, `scripts\run-gd.ps1`, `scripts\ZinkSession.psm1`, both `dist\zink\` and `dist\zink-clean\`, and `dist\tools\PresentMon-2.6.0-x64.exe`. Ship these alongside the `.geode` package while preserving the folder structure. Both Windows x64 driver bundles include their own `bundle.json` manifest.

Release maintainers can stage that folder and a zip from the built package with `scripts\package-user-release.ps1`. It refuses to overwrite either output path.
