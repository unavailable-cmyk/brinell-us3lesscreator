# V2 milestone 3B continuation: partial results

Milestone 3B is **not complete**. Peaceful's actual V2 ownership and eliminated native work remain zero. V2 remains opt-in and the unresolved trigger and dependency gates remain in force.

## Controlled native fixture

The Geode game fixture creates 64 decorations and two native translation routes, with overlapping membership. In the explicitly requested structural test mode only, it assigns parent mode -1, then alternates rotation plus a non-default anchor, nonuniform scale, skew, and two-axis flip. Native `updateTransform()` supplies the quad baked for V2. At level time 0.4 the fixture requests a further native rotation on one object of each kind; native boundaries must materialize the translation and restore stored transform metadata before the setter runs. No fixture mutation is enabled for named levels.

In the paired 3-second runs, V2 reached 64 owned objects, three chains, at most two active chains and three dirty entries. Four objects handed off for native rotation, and the other 60 later handed off at the visibility boundary. The same-tick captures at 0.7541667 seconds and tick 181 have matching player coordinates and zero changed channels in the decoration region. All four reported native positions and rotation, scale, skew, anchor, and flip values matched after handoff. The instrumented V2 reference position error was zero. These are narrow controlled results, not full-level visual or gameplay parity. A previous structural run without the handoff and non-default anchor differed at one pixel; no conclusion about general pixel parity follows from one matching capture.

The observed run reported 22,552 eliminated native movement member visits, 37,065 remaining native visits, 37,066 native section callbacks, 64 handoff commits, 53,005 native atlas draws and 7,181 V2 draws over 11,000+ uncapped frames. V2 routine geometry upload and routine transformation readback were zero; reference validation adds per-object CPU visits and a capture readback, so these numbers are not an uninstrumented performance claim. CPU frame p50/p95/p99 was 0.2853/0.4594/0.5713 ms, and GPU 0.01568/0.01620/0.01768 ms, in this small controlled scene. Totals depend on uncapped frame count.

Production opt-in eligibility now allows exactly parent mode -1 when group parent, area parent and linked group are absent; zero was already allowed. Other nonzero modes remain rejected. This rule is supported by the native fixture and the existing V1 nonpositive check, not a proof of all negative modes or all parent interactions. Static rotation, scale, skew and flipping remain restricted to the explicit fixture pending broader native correctness and visibility/handoff validation. The recorded frame comparison can be rerun with `scripts/compare-v2-fixture.py` against the paired report directory; PPMs, JSON reports and execution hashes are included in the review archive.

## Peaceful census

The six-second instrumented native observation examined all 273,268 loaded objects, including initially invisible objects, and 16,617,617 recorded movement-member visits. Current live eligibility and peak ownership are zero; eliminated visits are zero. The level-wide unresolved dependency flag applies to all objects. Rejection reasons overlap and must not be added together.

| Independent reason | Objects | Observed move visits |
| --- | ---: | ---: |
| Unresolved level dependency / shake-level observation | 273,268 | 16,617,617 |
| Protected group | 191,314 | 16,567,389 |
| Local transform | 242,095 | 16,319,247 |
| Initially inactive | 268,042 | 14,991,831 |
| Initially unbatched | 268,143 | 14,937,894 |
| History / enter / fade | 64,036 | 13,937,774 |

When readiness and the unresolved level-wide veto are hypothetically excluded, the current structural mask yields 1,768 candidates and 96 distinct membership sets. A cumulative prospective screen that additionally ignores local transforms and permits the V1 nonpositive-parent rule yields **18,249 objects, 197 membership sets and 50,228 observed move visits**. This is an upper bound over the observed workload, not eligibility, owned objects, retained ownership, or a prediction of FPS. The underlying chains count sorted membership sets, not proven equivalent movement histories. The prior preliminary run observed 52,393 visits for the same prospective object screen; counts vary with the recorded native activity. A material/color 3C projection is not quantified because the required dependencies are unresolved.

The expensive census and native reference instrumentation distort frame timings. Those timings must not be used to infer the speedup of the shipping path.

## Remaining work

Prove static-transform parity across realistic geometry, numerical ranges, action sequences and atlas transitions before admitting them outside the fixture. Separate native-resolved player-X-lock recipients from authoritative position readers while retaining all other protection causes and remapping, then test the native route. Design visibility-independent ownership that preserves section activation and atlas membership without routine per-object synchronization. Connect shared history to native initial history, command stamps, overlapping routes and handoff materialization; the existing history prototype is not enabled live. Resolve color, shake, remapping and other dependencies before relaxing the Peaceful veto. Measure retained ownership, eliminated work and uninstrumented frame-time distributions against matched native runs.
