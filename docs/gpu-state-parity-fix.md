# State parity and catch-up stall correction — 2026-11-15

The Geode 00:53:19 run passed the three GPU arithmetic regressions and the
first 576-object live movement reference. No Vulkan device-loss message was
recorded in that log. Those checks did not validate GD's state side effects.
The last completed 16-frame diagnostic window averaged 183.186 ms and reached
771.194 ms. It contained 532 processMoveActionsStep calls (33.25 per rendered
frame), 7,464,945 move section callbacks and 7,407,136 rotation section callbacks.
Sampled child timings overlap inclusive parents and must not be summed.

Local signed MSVC dumpbin disassembly of the installed Steam GD 2.2091
moveObjects (RVA 0x3368d0) shows the previously missing operation:
before arithmetic, when object+0x512 is false and its command stamp at 0x4dc
differs from the layer command index, save double position at 0x3b8/0x3c0
as floats at 0x4d0/0x4d4, update the stamp, and mark both rect flags at
0x368/0x369 dirty. Then update position, mark 0x351/0x352 dirty, and call
the section update followed by the original disabled-object finalizer.

The hybrid movement application now performs this history and bounds update
before mutating coordinates. Compile-time field offset checks tie the member
names to the disassembly. A shared constexpr history helper verifies repeated
commands retain the original prior position, a new command refreshes it, and
decoration objects skip this bookkeeping.

The previous cap allowed one blocking readback per simulation step. Catch-up
therefore multiplied the waits within one rendered frame. A frame budget now
resets at entry to CCDirector::drawScene and permits one GPU batch across all
of its simulation steps. Other helpers immediately execute vanilla, preserving
their original order and same-step state. GPU work is still synchronous.

All-object rotation remains vanilla: live rotation parity has not been
established. Inspection also found that rotateObjects clears object+0x513;
the isolated pilot had incorrectly set it. This flag was corrected. Broad
rotation offload must await validation of the full matrix and state behavior.

These fixes address concrete defects; the build and static checks cannot prove
that every trigger mode or the reported visual corruption is fixed. No new
in-game test or measured frame-time improvement is claimed by this document.
