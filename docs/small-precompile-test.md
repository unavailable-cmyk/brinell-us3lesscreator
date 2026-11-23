# Small transform-cache test level

Create a new local level named exactly `Vulkanizer Cache Test`. It is a separate
level; do not edit Peaceful. Use a flat floor and an unobstructed path so a
normal attempt can pass the triggers.

* Add 16 freely rotatable decoration objects in a visible cluster. Put all 16
  in group 100; also put half of them in group 102. Do not use solid blocks.
* Add one stationary decoration as the rotation center in group 101. Keep it
  out of groups 100 and 102.
* Place a normal move trigger targeting group 100: X = 120, Y = 0, duration
  2 seconds, linear easing.
* Place a normal rotate trigger slightly later: target 100, center 101,
  90 degrees, duration 2 seconds, linear easing.
* Place a normal move trigger slightly later: target 102, X = 0, Y = 60,
  duration 2 seconds. The half-cluster should move relative to the rest.
* Add a separate move trigger with **Touch Triggered** enabled, targeting
  group 102, X = -60, Y = 0, duration 1 second. Place its activation area on
  the path. Its movement must happen only when actually touched. If the
  editor's touch setup requires an activation object or different placement,
  use GD's normal touch-trigger setup; do not substitute a spawn trigger.

Leave enough space between triggers to distinguish the effects and enough level
length to watch them. Avoid randomized, follow, remap, or other dynamic triggers
in this first test. Those should receive dedicated cases later.

The automated test runner can select an exact local level name with
`-LevelName 'Vulkanizer Cache Test'`. The runner opens the existing local level;
it never edits or saves the level. It does not provide touch input, so the touch
case requires a separate controlled input/manual check. Cache preparation is not
proof that an event activated or that CPU work was eliminated; examine the live
precompile, scheduler, visual-stream and fallback markers.

The candidate package and test selector are unvalidated until a build and live
native comparison complete. Keep the installed validated package until then.
