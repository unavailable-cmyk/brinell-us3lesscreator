# Live rotation integration gap — 2026-11-15

The user run `Geode 2026-11-15 11.21.39.log`, session
`dist/Vulkanizer-Windows-x64/dist/sessions/20261115-112138-70a15a33`, confirmed
Zink Vulkan on the RX 7600. However, every transformation status window
reported zero optimized visits, zero rejected visits, no live GPU rotation
application, and no runtime-disable fuse. This contradicts the earlier
interpretation that mixed-array eligibility rejection explained non-activation.

Native GD 2.2091 `processRotationActions` at RVA 0x334450 contains its own
per-object transformation loop. Local dumpbin output saved in
`build/diagnostic-reports/gd22091-processRotationActions.txt` shows direct
position math, Cocos world-transform calls, `GameObject::addRotation` at
0x334b13/0x334c01, and section/finalizer virtual calls. It does not call
`rotateObjects` at RVA 0x334da0 in this routine.

The installed Geode log confirms the `rotateObjects` hook was enabled, so
hook registration alone did not establish gameplay coverage. The controlled
fixtures explicitly invoked that helper; their state parity and timing results
remain helper-level results, not proof of a live rotation replacement.

The latest completed light frame windows included 14 frames averaging
147.692 ms, then 4 averaging 692.945 ms, and 2 averaging 1032.418 ms.
That last window recorded 521 simulation steps across two rendered frames.
No transform optimization can be credited for this run's subjective stability.

The next implementation must replace/intercept the actual scheduler's
inlined transformation work while preserving action bookkeeping and ordering,
and must demonstrate nonzero live entry/application counts. Changing
eligibility or optimizing helper readback alone cannot address this gap.
No source/runtime fix or new installed package is claimed by this finding.

Update: the actual scheduler replacement and verified live activation are now documented in [rotation-scheduler-integration.md](rotation-scheduler-integration.md). The original finding above remains a record of the earlier inactive helper path.
