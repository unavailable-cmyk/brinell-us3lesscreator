# Brinell

Brinell is a clean, CPU-focused optimization project for Geometry Dash through the Geode mod framework.

## Scope

- Shared-chain CPU transform batching
- Conservative transform culling
- Native-state ownership and replay validation
- CPU frame timing and callback diagnostics

This repository intentionally contains no external renderer integration, graphics-driver replacement, or compute-backend implementation.

## Requirements

- Windows x64
- Geometry Dash 2.2091
- Geode 6.0.0-alpha.1
- C++23
- Visual Studio 2026 / MSVC 19.50

## Build

Configure `GEODE_SDK` to the pinned Geode SDK, then configure and build with CMake:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config RelWithDebInfo --parallel
```

The final package is written by Geode to `build/`.
