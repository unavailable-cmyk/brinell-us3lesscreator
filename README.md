# Vulkanizer

## Architecture V2 foundation

An independent shared-translation scene compiler, chain scheduler and persistent GL 3.3 renderer are available in `src/v2`, with controlled benchmarks at 1, 1000, 10000 and 100000 decorations. Production game integration and Peaceful/POCO performance validation remain pending. Architecture, ownership contracts and reproduction are documented in [VulkV2](docs/vulk-v2.md).

Geode mod and experimental Windows **OpenGL → Mesa Zink → Vulkan** integration for Geometry Dash's original window.

The Geode integration and instrumented **Mesa 26.2.6 Windows x64 bundle are built**. Static checks passed for architecture, dependencies and GL/WGL exports. The hidden WGL API check encountered Windows App Control error 4551 while loading `gallium_wgl.dll`. Actual GD rendering through Zink awaits the manual integration session and matching API evidence.

## Build the mod

```powershell
.\scripts\setup.ps1
.\scripts\build.ps1
```

The official `geode new` baseplate, SDK integration, generated hooks and `geode build` packaging are preserved. The exact target is Windows x64, GD **2.2091**, Geode SDK/Loader **6.0.0-alpha.1**, Geode CLI **4.0.0**, and C++23. Tested compiler: MSVC 19.50 / Visual Studio 2026, CMake 4.3, Windows SDK 10.0.26100.0. Setup uses isolated `.tools` paths and does not overwrite the user's older SDK or global environment. CMake rejects a different SDK.

Package: `build/us3lesscreator.vulkanizer.geode` (also delivered under `dist`). The preserved bootstrap package, source snapshot and in-game log are in `dist/phase0`.

## Actual renderer integration

The viable initial route is Mesa's Windows WGL frontend with Zink selected before GD starts. It translates GD's existing OpenGL commands, including game/Cocos/GLEW paths, rather than replacing only a subset of sprite hooks. Driver selection cannot safely be changed after the game's WGL contexts and textures already exist.

The current mod observes its real render context and logs `GD_RENDER_ROUTE`: PID, original HWND, renderer and loaded `opengl32.dll` path. Neither a renderer string nor Vulkan availability alone is accepted as proof.

See [approach analysis and evidence requirements](docs/gd-vulkan-integration.md). Mesa 26.2.6 source was downloaded from the official archive and checked against its published SHA-256. Small source instrumentation counts **actual Zink Vulkan draw dispatches** and logs successful Vulkan surface/present calls with HWND/PID. Its file checksums and all changed call sites are recorded. This instrumented development build disables the shader disk cache to reduce initial build dependencies; it is not a performance benchmark build.

## Build Zink

The build script locates Python through `py -3` and invokes `-m mesonbuild.mesonmain`. The successful build used Meson **1.12.1**, MSVC 19.50 and the existing verified Mesa source and source-built WinFlexBison. Project-local PyYAML is available under `.tools/mesa-python-deps`.

```powershell
.\scripts\build-zink.ps1 -BuildOnly
.\scripts\build-zink.ps1 -BuildOnly -Uninstrumented
```

`-BuildOnly` uses Meson's supported cross-build configuration for Windows x64. It performs compile/link checks and defers target execution checks. This was needed because Windows blocked Meson's compiled compiler sanity program. A normal native configure remains available by omitting the switch on a machine that supports those runtime checks.

The bundle under `dist/zink` contains `opengl32.dll`, `gallium_wgl.dll`, hashes, license and the instrumentation manifest. The hidden API check is available after Windows accepts the DLLs through the machine's development trust workflow.

```powershell
cmake --build build-native --config Release --target wgl-zink-api
.\scripts\check-zink-api.ps1
```

That check exercises GDI pixel formats and presentation, Cocos/GLEW entry-point resolution, textured GL draws, two pixel readbacks and resource cleanup. Its validator requires correlated Vulkan draw/present counters for the test window. GD has its own evidence gate described below.

## Human-led integration test

Close GD and install the new `.geode` into the exact-target installation's `geode/mods`. The previously encountered Windows Smart App Control block on unsigned mod binaries must also be resolved through an approved development/trust workflow before loading the new mod; this project does not change security settings.

```powershell
# Normal fallback; stages no graphics DLLs
.\scripts\run-gd.ps1 -Backend OpenGL

# Experimental session
.\scripts\run-gd.ps1 -Backend Zink -ZinkDescriptors auto
```

The Zink session temporarily stages only its two hash-verified DLLs beside GD, refuses any pre-existing files with those names, and restores the original experimental settings and removes only its own unchanged DLLs after GD exits. `-ZinkDescriptors auto|lazy|db` selects Mesa's descriptor manager for an A/B run. The default `dist\zink` bundle is instrumented for Vulkan evidence; `-BuildOnly -Uninstrumented` creates a clean performance bundle in `dist\zink-clean`, which can be launched with `-MesaDirectory .\dist\zink-clean`. No executable is patched; no global graphics DLL or registry setting is changed. If the launcher is interrupted, close GD and use `run-gd.ps1 -RestoreOnly` with the same game/settings paths. Always recover before launching GD normally. Do not close the launcher while the test is running.

**What to report manually:** Does the original GD window show the menu and a familiar level correctly? Are sprites/textures, labels, clipping and blending intact? Does resizing or a windowed/fullscreen change corrupt or freeze rendering? Does OpenGL mode still work after exiting the Zink session? Report the level, visible issue and session folder; screenshots are optional and needed only for unclear defects. No repeated agent screenshot checks are required.

After closing GD, check objective evidence using the session's PID from `session.json`:

```powershell
& 'C:\path\to\approved\python.exe' .\scripts\verify-gd-vulkan.py `
  --geode-log .\dist\sessions\SESSION\geode-vulkanizer.log `
  --mesa-events .\dist\sessions\SESSION\mesa-vulkan.jsonl --pid PID
```

It requires Zink in GD's context, Vulkan draws, and at least 120 Vulkan presents **to that same PID and original HWND**, with no recorded presentation errors. It does not certify visual correctness or measure GPU timings. Missing evidence means **NOT_CONFIRMED**.

## Automated checks

```powershell
cmake -S tests -B build-native -G 'Visual Studio 18 2026' -A x64 `
  '-DPython3_EXECUTABLE=C:/path/to/approved/python.exe'
cmake --build build-native --config Debug --parallel 6
ctest --test-dir build-native -C Debug --output-on-failure
```

Checks cover frame statistics, synthetic evidence rejection/correlation, session rollback and file-protection rules, plus a compile check for Windows telemetry. The historical preview target remains excluded from the default build/tests.

Both mod settings default to off. The experimental setting alone does not switch a graphics driver; the Zink launcher sets it temporarily for its restart-only session. Frame diagnostics log CPU intervals and p50/p95/p99. Full-frame GL draw counts and GPU timing remain unavailable. No speedup or Peaceful benchmark is claimed.

The current artifact is for local testing. AI-generated mods are not eligible for the official Geode Index.
