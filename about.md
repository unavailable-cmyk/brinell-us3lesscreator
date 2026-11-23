# Vulkanizer

Experimental integration for **GD 2.2091 / Geode 6.0.0-alpha.1 / Windows x64**.

The current milestone is translating **GD's own rendering through Mesa Zink to Vulkan in its original window**. That route is under development and has not yet passed the full runtime evidence check. No speedup is claimed.

The mod logs its startup version and checks GD's actual graphics context. It does not open a separate Vulkan preview.

- **Experimental Zink session:** checks the selected route. The Vulkanizer launcher must select Zink before GD starts; this toggle alone cannot replace a loaded graphics driver.
- **Frame diagnostics:** optional CPU frame intervals, FPS and percentiles. GPU timing is unavailable.

Both settings default to off and require a restart. Use the launcher's OpenGL mode as the fallback. See the project README before starting a Zink test; session files must be restored before launching GD normally.
