# Tuning reference

All values are in `Source/HeatlineMX/Core/MXTuning.h`, with the shipped defaults as C++ initialisers.
You can override them in the editor through these Data Assets:

| Asset | Class | Holds |
|---|---|---|
| `/Game/HeatlineMX/Data/DA_BikeTuning` | `UMXBikeTuning` | speed, surfaces, heat, lanes, pitch, flight, landing, barriers, crash/recovery, contact |
| `/Game/HeatlineMX/Data/DA_CameraTuning` | `UMXCameraTuning` | camera per viewport shape (Full, Wide 2P top/bottom, Tall 2P side-by-side, Quarter 3P/4P, Designer) |
| `/Game/HeatlineMX/Data/DA_AITuning` | `UMXAITuning` | Easy / Medium / Hard skill |
| `/Game/HeatlineMX/Data/DA_TrackStyle` | `UMXTrackStyle` | track scale (m per NES column / row), lane width, checkpoint spacing, colours |

`BP_MXBike` exposes presentation-only values: suspension stiffness and damping, landing-compression
gain, lean, and dust rate. None of the presentation values change the simulation.

Units: metres, seconds, degrees, heat in % (0–100). The simulation runs a fixed 120 Hz step
(`FMXBikeSim::FixedDt`).

## Speed and surfaces
| Value | Default | NES reference / why |
|---|---|---|
| MaxSpeedNormal / MaxSpeedTurbo | 27.2 / 30.5 m/s | NES A = 800, B = 832 velX. At 1.25 m/column, 832 → 30.5 m/s. Turbo gets +12 % (NES +4 %) so it's worth managing heat. |
| AccelNormal / AccelTurbo | 11 / 26 m/s² | NES +24 / +63 per 4 frames. Turbo's real edge is acceleration. |
| CoastDecel | 9 m/s² | NES "let go and the brakes operate" (−56/4 f is ~31 m/s² scaled, too abrupt in 3D) |
| OverCapDecelNormal / Turbo | 3 / 0.4 m/s² | NES A above cap bleeds speed; B holds it. Keeps flow boosts alive with turbo. |
| SlopeGravity | 14 m/s² | Replaces the NES "downhill take-off" +256 boosts with continuous slope gravity |
| AbsoluteSpeedCapFraction | 1.15 | Caps flow speed at 35 m/s |
| MudSpeedCap / Turbo / MudDecel | 0.55 / 0.45 / 38 | NES mud friction 127, or 192 with B held (turbo digs in) |
| GrassSpeedCap / GrassDecel | 0.5 / 26 | NES "track missing" tile E4: throttle ignored, −56 |
| VergeSpeedCap / VergeDecel | 0.6 / 20 | NES rough strip beyond the outer lanes |

## Heat
| Value | Default | NES reference |
|---|---|---|
| HeatRateTurbo | 14.7 %/s | +15/256 per frame → 24 units in 6.8 s |
| HeatRateNormal / HeatEquilibriumNormal | 6.8 %/s / 37.5 % | A: equilibrium 17 of 8..32 |
| CoolRate | 10.7 %/s | −11/256 per frame above equilibrium |
| HeatWarning | 75 % | NES warns at 26 of 32 |
| StallDuration | 3.5 s | NES 264 frames = 4.4 s, shortened for quicker retries |
| CoolStripHeat | 0 | NES cool zone sets the temperature to 8 (cold), on the ground only |

## Pitch and flight (the pitch decision is in DESIGN.md)
| Value | Default | Notes |
|---|---|---|
| Gravity | 40 m/s² | Tuned so NES ramp spacing gives NES-like rhythm (neutral medium-ramp jump at turbo ≈ 22 m / 0.9 s) |
| AirPitchRate | 150 °/s | NES: one angle step (~15°) every 4 frames |
| AirPitchMax / Min | +75 / −60° | NES angle range 2..11 |
| NoseUpGravityScale / AirDragNoseUp | 0.5 / 12 m/s² | NES Left: gravity term 24 instead of 52, friction 60 |
| AirDragNeutral | 4 m/s² | NES 28 (much harsher) |
| NoseDownVerticalTimeScale / DiveThrust | 0.75 / 0.8 | NES Right: vertical step skipped every 4th frame, friction 0 |
| PopBonus / ScrubPenalty / LipInputWindow | 0.12 / 0.2 / 0.15 s | Modern take-off technique |
| MaxLaunchAngle | 36° | Caps the take-off path angle while keeping the speed. Without it, the steep ramps' 60°+ faces threw the bike almost straight up (9.5 m apex). The NES has no equivalent: its launch speed comes from a per-ramp table, not the face angle. |
| LandingAssistRate | 25 °/s | Per-player option |
| WheelieAngle / WheelieCrashAngle | 38 / 72° | NES wheelie flips at angle 13 |

## Landing
| Value | Default |
|---|---|
| PerfectTolerance | ±7° |
| Clean window | −15° … +20° |
| Crash window | < −35° or > +50° (up-faces: < −25°, down-faces: up to +55°) |
| CleanSpeedLoss / WobbleSpeedLoss | 4 % / 18 % |
| WobbleBounceFactor / WobbleNoDriveTime | 0.35 / 0.4 s |
| HardLandingNormalSpeed / LossPerMS | 16 m/s / 2 % per m/s |
| FlowBoostPerfect / Clean | +8 % / +4 % (down-slope landings) |

## Barriers, crashes, contact
| Value | Default |
|---|---|
| BarrierCrashSpeedFraction / BarrierSafePitch | 0.75 of turbo speed / 12° (NES: angle below 7 and at least 2.5 px/f) |
| CrashTumbleBase + PerSpeed | 0.6 s + 0.8 s × speed fraction |
| RecoveryBase / MashCut / Min | 1.4 s / 0.12 s per press / 0.5 s |
| GhostAfterRecovery | 2 s without collisions |
| RearEndCrashSpeed | 3 m/s closing speed |
| CutInWindow / CutInSpeedLoss | 0.5 s / 25 % |
| ContactReactTime | 0.5 s. A rear-end only crashes if both riders have been on the ground this long; otherwise it's a wobble. |
| ContactCrashCooldown | 6 s |

## Camera (per viewport shape)
The camera frames the bike: a fixed orientation, placed back along the line of sight that puts the bike
at (`ScreenX`, `ScreenY`).

| Shape | Distance | FOV (horizontal) | Pitch | Yaw | ScreenX / ScreenY | SpeedScreenShift | Height / lateral follow |
|---|---|---|---|---|---|---|---|
| Full (1P) | 26 m | 60° | 18° | 27° | 0.33 / 0.62 | 0.07 | 0.35 / 0.3 |
| Wide (2P top/bottom) | 22 m | 84° | 13° | 18° | 0.30 / 0.60 | 0.05 | 0.45 / 0.25 |
| Tall (2P side by side) | 36 m | 58° | 22° | 20° | 0.46 / 0.64 | 0.08 | 0.3 / 0.5 |
| Quarter (3P/4P) | 24 m | 64° | 18° | 28° | 0.29 / 0.62 | 0.07 | 0.35 / 0.3 |
| Designer | 52 m | 55° | 26° | 0° | 0.5 / 0.5 | 0 | 0 / 0 |

`FollowLag` (0.15 s) and `HeightLag` (0.35 s) smooth the lateral and height following. Along the track
the camera is locked to the bike. `VisibleLookAheadSeconds` measures how far along the rider's own lane
the track stays inside the view at the current speed. The `shots` autotest requires at least 1.0 s in
every layout.

## AI
| | Easy | Medium | Hard |
|---|---|---|---|
| ReactionDelay | 0.45 s | 0.25 s | 0.1 s |
| LandingErrorDeg | 16 | 10 | 3 |
| TurboHeatLimit | 65 % | 78 % | 94 % (plans cool strips) |
| TurboResumeMargin (turbo again once heat is this far below the limit) | 25 % | 22 % | 4 % |
| TurboMinRest (shortest wait before pressing turbo again) | 4.5 s | 0 | 0 |
| AirCoastChance (per jump: off the gas in the air, which cools the engine) | 0 | 10 % | every jump |
| LaneHorizon | 20 m | 45 m | 70 m |
| Flight control (float/scrub choice), FlightPlanChance | no | 20 % of jumps | every jump |
| MashRate | 4/s | 7/s | 10/s |
| MistakeRate | 0.15 | 0.05 | 0.015 |
| GreedyTurboRate (chance per second of riding turbo into the red for up to 6 s) | 0.007 | 0.004 | 0.001 |
| Chasers | no | yes | yes |

Easy and Medium ride the heat gauge the way people do (changed 2026-09-27 after a player report that the
AI "used turbo nonstop without overheating"). Turbo comes in bursts. Through a jump they keep holding what
they held at take-off, as most players do, so a turbo burst goes on heating the engine in the air, where
turbo does nothing (NES, see RESEARCH_ORIGINAL.md §3). Letting go of everything in the air stops that and
cools the engine, below the 37.5 % resting heat too. Before this change every level let go in every jump,
so even Medium had turbo on 83 % of its time on the ground. Only Hard still does. Now Easy has turbo on
about 40 % of its ground time and Medium about 57 %. Both overheat now and then (visible steam, then a
stall), and Hard is at about 91 %. A different limit or resume margin cannot lower the share. Above the
resting heat, turbo heats at 14.7 %/s and cooling runs at 10.7 %/s, so any limit gives the same 42 %
duty. Easy's minimum rest is what slows it.

Medal targets (`UMXAITuning`): `MedalGoldFactor` 1.03, `MedalSilverFactor` 1.10 and `MedalBronzeFactor`
1.20, each times `FMXTrackValidator::ReferenceTime` for the layout and lap count. For example, the 2-lap
Challenge layout of Course 1 has a reference of 60.2 s, giving gold 62.0 / silver 66.3 / bronze 72.3 s.

Every level uses the same input and physics as a player, so the difference between levels is skill, not
speed. The flight-control step (Medium and Hard) flies each candidate air plan (neutral, nose-down,
nose-up, half of each) with the real `FMXBikeSim::Step`. It uses the same in-flight controller the rider
will then follow: hold the plan's input, then switch to lining up with the predicted landing surface
while there is still time to rotate. The plan that reaches furthest by a common horizon wins, and any
crash costs 150 m. Plans are chosen this way because the pitch input changes the trajectory: a plan that
looks good with a fixed input can be impossible to line up in time. Barrier wheelies use a partial lift
(0.6 input, about 27°). That clears the front-wheel rule without the over-rotation a held full lift causes.

The offline benchmark (8 seeds × 5 courses × 2 layouts, see TESTING.md) gives this order on every layout:
Hard finishes 3.5–11.4 s ahead of Medium, and Medium 4.6–9.6 s ahead of Easy. Hard never overheated and
averaged at most 0.4 crashes per race. Easy averages about 0.5 overheats per race and Medium about 0.35.
Both also make occasional landing and barrier mistakes.

The levels are calibrated against three player styles in the offline `human` mode (24 seeds × 5 courses).
Each style is the same AI brain, set up the way people ride: the gas stays held through jumps, and turbo
comes off at the heat warning. Real players make more mistakes than these styles, so each level is set to be
slower than the style it is meant for:

| Style | Total over the 5 courses | Matched level | Level's total |
|---|---|---|---|
| Novice (turbo off at the 75 % warning, gas held in every jump) | 384.0 s | Easy | 421.0 s (+9.6 %) |
| Casual (turbo to 85 %, off the gas in 30 % of jumps) | 375.3 s | Medium | 384.9 s (+2.6 %) |
| Good (turbo to 92 %, off the gas in 90 % of jumps, plans jumps and cool strips) | 352.4 s | Hard | 352.8 s (+0.1 %) |

## Obstacle profiles
Heights are in NES rows (0.6 m) and lengths in columns (1.25 m), scaled to each piece's length. The
values live in `FMXObstacleLibrary`.

| Piece | Profile (column, row) |
|---|---|
| RampSmall | (0,0) (1.5,2) (3,0) |
| RampMedium | (0,0) (2.5,3) (5,0) |
| RampLarge | (0,0) (4.5,5) (9,0) |
| TableLow | (0,0) (2,2) (7,2) (9,0) |
| RampSteep | (0,0) (2.5,5) (5,0), a plain peak. The earlier kinked face launched at 60°+. |
| RampSteepBack / RampSteepFace | (0,0) (3.8,4) (6,0) / (0,0) (1.8,4) (6,0) |
| Kicker | (0,0) (2,2.2) (2,0), a vertical back |
| FinishDeck | (0,0) (2.5,3) (9.5,3) (12,0) |

## Offline check
`python3 Tools/physics_proto.py` rides each NES course with a perfect-landing rider using the same
flight maths. It gives a quick sanity check of jump lengths against ramp spacing when you change
Gravity or speeds. Keep its constants in sync with the defaults above.

A second offline check needs no engine. It compiles the pure-logic C++ (types, tuning, obstacle
library, track model, validator, bike sim, AI, contact) against small UE shims with clang and runs
the automation-test checks, the NES course validation and drive tests, and the multi-seed AI
benchmark. See TESTING.md.
