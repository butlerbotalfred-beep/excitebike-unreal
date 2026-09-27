# Testing

Heatline MX has four kinds of testing. They prove different things and are reported separately.

0. **Engine-free offline check.** The gameplay simulation, compiled with clang and no engine (below).

1. **Headless automation tests.** These cover the pure gameplay layer, with no rendering or input
   devices. Run them with `Tools/ue.sh tests`; they live in `Source/HeatlineMX/Tests/MXTests.cpp`.
2. **Simulated-input end-to-end runs.** These run the real game. Synthetic key and axis events for
   *virtual* devices go through the same `UMXGameViewportClient` routing, Enhanced Input mapping
   contexts and player controllers that real controllers use. They are **not** physical-controller
   tests. Run them with `Tools/ue.sh autotest <scenario>`; reports go to `Saved/AutoTest/<scenario>.json`,
   with screenshots.
3. **Physical-controller checks.** These need a person holding the real gamepads and keyboard. The
   checklist is below.

## 0. Engine-free offline check
`Tools/offline_check/run.sh` compiles the pure-logic C++ with clang against small UE shims, with no
editor or engine. That covers types, tuning, the obstacle library, the track model, the validator, the
bike sim, the AI and contact. It runs the same checks as the automation tests, validation and physics
drive tests of all five NES courses, and AI races on both layouts of every course.
`run.sh ai 8` runs the multi-seed AI benchmark; `run.sh trace …` prints a step trace of one AI run;
`run.sh human 24` races three player styles against each AI level (TUNING.md, "AI").
This checks the simulation only; rendering, input, UI and Unreal integration are not covered.

Latest offline results (2026-09-27, after the AI turbo change):
- 87 checks passed, 0 failed. All five courses validate, and the drive test flags no impossible pieces.
  The 3 new checks mirror `HeatlineMX.AI.TurboLikeAPerson`.
- AI benchmark, 8 seeds per cell. Mean race times in seconds for 2 laps:

| Course / layout | Easy | Medium | Hard | Hard crashes / overheats per race |
|---|---|---|---|---|
| 1 challenge / main | 69.3 / 76.0 | 64.7 / 71.1 | 60.4 / 65.3 | 0 / 0 |
| 2 challenge / main | 64.0 / 68.8 | 58.4 / 63.9 | 54.7 / 58.9 | 0 / 0 |
| 3 challenge / main | 77.2 / 85.9 | 70.0 / 76.3 | 66.5 / 70.8 | 0 / 0 |
| 4 challenge / main | 88.9 / 100.6 | 79.3 / 95.2 | 72.0 / 83.8 | 0–0.25 / 0 |
| 5 challenge / main | 83.8 / 87.7 | 77.1 / 81.5 | 66.1 / 71.8 | 0–0.38 / 0 |

  Easy averages 0–0.6 crashes and 0.1–0.8 overheats per race; Medium averages 0–0.6 crashes and 0.1–0.5
  overheats. Easy has turbo on about 40 % of its time on the ground, Medium about 57 %, and Hard about 91 %.
  Hard's times are unchanged. The 2026-09-24 run had Easy at 58–91 s and Medium at 57–85 s, before
  they stopped using the expert cooling trick.
- Player styles vs AI levels (`run.sh human 24`): Easy is 9.6 % slower than the novice style, Medium 2.6 %
  slower than the casual style, and Hard level with the good style. The table is in TUNING.md.
- Turbo choices (`run.sh choices`): the Hard AI's rider logic with the turbo policy overridden, main
  layouts, averaged over the four start lanes. Holding turbo all the time is never the answer: it
  overheats 1.5–3 times per race and loses 4.5–8.7 s to heat management. Never using turbo loses
  8.5–16.8 s. Turbo "feathered" just under the red line comes within ±1 s of the managed Hard rider;
  keeping heat just under the limit is the skill, as on the NES.

| Course (main) | Managed (Hard) | Turbo to the red line | Hold turbo always | Never turbo |
|---|---|---|---|---|
| 1 | 64.8 | 64.7 | 69.3 (1.5 overheats) | 74.2 |
| 2 | 58.2 | 58.2 | 66.3 (2.5) | 67.3 |
| 3 | 71.4 | 70.8 | 80.1 (3.0) | 82.2 |
| 4 | 83.7 | 84.2 | 89.1 (1.75) | 100.5 |
| 5 | 69.8 | 70.8 | 73.2 (1.5) | 82.0 |

- 8-bike AI races with contact (`run.sh pack 6`; main layouts, mixed difficulties, 6 races per course).
  Every rider finished every race. Contact crashes were 0–1.2 per race, 1.8 in total across the five
  courses (2.0 before the AI turbo change). No rider was contact-crashed more than once in a race. Hard won
  all 30 races; before the change Hard won 26 and Medium 4.
- What the check found and fixed:
  - (2026-09-27, from a player: "all the AI racers use turbo nonstop without overheating, so I cannot keep
    up") Every AI level let go of turbo and the gas in every jump, an expert NES trick most players never
    use. It also rode turbo to a precise heat limit. So Medium had turbo on 83 % of its ground time and was 4–6 %
    faster than a human-style rider who never overheats. Easy and Medium now keep holding through jumps,
    use turbo in bursts and sometimes overheat. They are calibrated against player styles with the new
    `human` mode.
  - The steep ramp launched at over 60°, giving a 9.5 m apex. Fixed by a plain peak profile plus `MaxLaunchAngle`.
  - Hard was slower than Medium on some courses. Mid-air plans could not be lined up in time, and
    riders swerved into barrier lanes too late. Fixed by choosing air plans through physics rollouts,
    checking barriers in the target lane, and using a partial-lift wheelie.
  - AI turbo stopped at the speed cap. Fixed with turbo hysteresis plus a per-skill greedy mistake.
  - The AI's pre-jump speed plan slowed riders for crests without saving any landings, costing up to
    1.9 s. Hard also held turbo back too early (88 % limit, resuming only after cooling 22 %). Removed
    the speed plan (the rollout air planner handles every jump at speed) and made the resume margin a
    per-skill value (Hard: 94 % limit, resumes 4 % below it).
  - A rider bumped back over the lap line who crossed it again got the lap counted twice, which could
    end their race a lap early. Fixed in `MXProgress`, and covered by `Race.Progress` (run against the
    old logic, the test fails).
  - Medal targets came from a distance formula that even Easy AI beat for gold (Course 1: gold 69 s,
    Easy 63.8 s). The results screen also ignored custom lap counts: at 4 laps no medal was possible,
    and at 1 lap every finish was gold. Targets now come from a measured reference run for the exact
    layout and lap count (DESIGN.md, "Race rules").
  - AI contact crashes ran at 1.5–3.3 per 8-bike race, mostly riders landing behind someone who had
    just landed and slowed. Fixed with the contact landing rule (DESIGN.md) and AI avoidance: move
    over, or ease off, before reaching a slower rider.

## 1. Automation tests
| Test | What it proves |
|---|---|
| `HeatlineMX.Bike.Heat` | Turbo overheats in about 6.8 s from cold. Normal throttle settles at 37.5 % and never overheats. Cooling runs at 10.7 %/s. The stall lasts `StallDuration` and resets heat. Cool strips reset heat on the ground. |
| `HeatlineMX.Bike.Landing` | Landing classification table (flat, up-face, down-face). The same jump repeated gives an identical distance and grade (determinism). |
| `HeatlineMX.Bike.PitchChangesTrajectory` | Pitch input changes the flight: nose-up flies higher and hangs longer, nose-down flies farther than neutral and than nose-up. |
| `HeatlineMX.Track.Obstacles` | Per-lane heights and surfaces: large-ramp peak, mud lanes 1 and 3, barrier lanes 3–4, platform deck over lanes 1–2 only, grass lanes, verge. A barrier hit at speed crashes; a wheelie clears it. |
| `HeatlineMX.Race.Checkpoints` | Gates are strictly ordered, with one lap line per lap and the final gate as the finish. A recovery point is never ahead of the crash point. |
| `HeatlineMX.Race.Progress` | Checkpoint integrity (`MXProgress`, shared with the race manager). A clean ride counts each gate once and finishes in order. A forward teleport grants nothing, and a gate skipped that way can never be crossed. A recovery behind a crossed gate doesn't count it twice. Being bumped back over the lap line un-counts that lap, so crossing it again counts it once. |
| `HeatlineMX.Race.Contact` | Rear-end → the rear rider crashes. Cut-in → wobble only. Either rider only just landed → a bump, and the rear rider is held behind. Cooldown prevents chains. Side contact is never a crash. Ghosted riders pass through. |
| `HeatlineMX.Track.NESCourses` | All five courses load with 2 laps, and main and challenge lap lengths match the manifest. IDs are stable and there are no validation errors. |
| `HeatlineMX.Track.Validator` | Start-zone, overlap, lap-end and missing-finish errors are reported, and auto-fix adds a finish. Lane-separated surfaces are allowed. JSON round-trips. |
| `HeatlineMX.AI.CompletesCourse` | Easy, Medium and Hard AI each finish Course 1 on the same physics. Hard beats Easy on skill alone. |
| `HeatlineMX.AI.TurboLikeAPerson` | Over 10 races (five courses × 2 seeds): Easy has turbo on under 55 % of its ground time, never lets go in the air and overheats at least once. Medium stays under 70 % and lets go in under 30 % of its air time. Hard lets go in over 90 % of its air time. |

## 2. Simulated-input scenarios
| Scenario | Steps and checks |
|---|---|
| `loop` (4 virtual gamepads) | 1. Title → menu → Local Versus. 2. Four devices join; colours stay unique; a player leaves and rejoins; all ready → course select. 3. Race with 4 local players (2×2 split); each player possesses their own bike. 4. **Independence:** only P1 holds turbo (only P1 heats), only P2 changes lane, only P3 wheelies, P4 stays idle. 5. A sabotaged nose-down landing crashes P2 → P2 recovers, and the recovery never gains progress. 6. P3's controller disconnects → the race pauses and flags P3 → reconnect → resume. 7. Finish → results in validated order. 8. Rematch → finish → back to lobby (1 local player, 4 still joined). |
| `loopkb` | Same as `loop`, with P1 on the keyboard and three gamepads. |
| `designer` | 1. Open from the menu; place a piece with the controller; place more through the API; move, delete, undo, redo. 2. Validate with the physics drive test; save and load a named track; check the round trip; check the validator flags a broken track. 3. Test ride → back in the designer. 4. Race the saved track 2-player split screen from the lobby with the course preselected. |
| `perf` | 4 local players (autopilot through injected input) plus 4 AI on Course 4 at 1920×1080. Records frame time (avg, p50/p95/p99, max, 1 % low) and game, render and GPU thread times. |
| `shots` | Screenshots of the 1P, 2P top/bottom, 2P side-by-side, 3P (standings quadrant) and 4P layouts. **Camera checks per player view:** the player's own bike is projected to the screen and must sit where the camera frames it (left third, lower half). The rider's own lane must stay visible at least 1.0 s ahead at top speed. |
| `menus` | Title → main menu → options → time-trial lobby → controls remapping → course select (medal targets) → race → pause → results, each reached through menu input and captured. |
| `fonts` | Diagnostic: draws text through several canvas paths and captures them (see "Engine results"). |

Results: see the "Results" section, filled in from the latest runs.

## 3. Physical-controller checklist (needs a person)
Two Xbox Wireless Controllers are paired with this Mac over Bluetooth. Run the checklist on the packaged
app (`Build/Mac/HeatlineMX.app`). Its sandbox is the one difference from the editor runs that could
affect controllers. With the controllers switched on:
1. Title: press A on controller 1 → main menu → LOCAL VERSUS.
2. Lobby: press A on controller 2 → P2 joins. Press Enter on the keyboard → P3 joins as the keyboard player.
3. Each player changes colour (Left/Right) and readies (A / Enter) → course select → Course 1.
4. In the race, check that each device moves **only its own bike**: lanes, pitch, accelerate, turbo.
5. Turn controller 2 off mid-race → the race pauses with "P2 controller disconnected". Turn it back on,
   or press A on a free controller → Start resumes.
6. Finish → REMATCH → BACK TO LOBBY. Press B on a controller to leave.

## Results

### Engine results (Unreal Engine 5.8.3, Metal, M4 Max, macOS 26.6; 2026-09-25)
All runs used the Development editor binary in `-game` mode at 1920×1080 windowed. Scenario inputs
are **simulated** devices (synthetic events through the same viewport routing, Enhanced Input
contexts and controllers). They are not physical-controller tests.

| Suite | Result |
|---|---|
| Compile (`Tools/ue.sh build`) | Succeeds, 0 errors, 0 warnings |
| Automation tests (`Tools/ue.sh tests`) | 10/10 pass |
| `loop` (4 virtual gamepads) | 24/24 |
| `loopkb` (keyboard + 3 virtual gamepads) | 24/24 |
| `designer` | 15/15 |
| `menus` | 7/7 |
| `shots` (framing + look-ahead in every layout) | 24/24. Bike x 0.28–0.41, visible look-ahead 1.01–1.51 s |

**Performance** (`perf`: 4 local players + 4 AI, Course 4 main layout, 1920×1080, 60 s sample):

| | Value |
|---|---|
| Average frame | 8.33 ms (120 fps, locked to the display's 120 Hz) |
| p50 / p95 / p99 / worst frame | 8.2 / 9.3 / 9.5 / 13.9 ms |
| 1 % low | 104 fps |
| GPU time | 6.5 ms |
| Game thread | 1.8 ms |
| Render thread | 8.3 ms, including the display-sync wait (see below) |

**Packaged game** (`Tools/ue.sh package`, Development config, cooked + pak, `Build/Mac/HeatlineMX.app`, run through
`Tools/ue.sh packaged-autotest <scenario>`):

| | Result |
|---|---|
| `loop` | 24/24 |
| `menus` | 7/7 |
| `perf` (same setup as above) | 120 fps average; p50 / p95 / p99 / worst 8.4 / 9.0 / 9.4 / 9.8 ms; 1 % low 105 fps; game thread 1.2 ms; GPU 6.7 ms. No hitches (shaders are precompiled in the package). |

The 60 fps target (16.7 ms) is met with about 2× headroom on the GPU and game thread. The Metal RHI
always syncs to the display in windowed mode (`displaySyncEnabled` is forced on unless the game is
fullscreen), so this machine can't show an uncapped windowed frame rate. The render-thread figure
includes that wait.

**Problems the engine runs found (fixed; re-run above):**
1. **All canvas text was invisible.** A Slate composite font drawn through `FCanvasTextItem` renders
   nothing on UE 5.8 / Metal (the `fonts` diagnostic shows it). Text now uses the engine's Roboto
   `UFont` asset.
2. **Camera:** the look-at point 23 m ahead turned the view about 44° down the track, so each player's
   own bike was off the left edge of their view. The camera now frames the bike at a fixed screen
   position (see DESIGN.md, "Track structure and camera").
3. **3P standings** were drawn over P1's view. The engine leaves the overlay canvas sized to the last
   player's view rectangle, so it is now reset to the full window.
4. **Performance:** 22 fps average (render thread 44.5 ms). Every procedural mesh section was rebuilt
   into draw commands each frame for 4 views and their shadow passes. Track chunks, ground, stands,
   gantries and bike parts are now runtime-built static meshes with cached draw commands.
5. **Static-mobility meshes** can't take a mesh after registration. After the change above, the track
   was briefly invisible (and one perf run measured without it); meshes are now assigned before
   registering.
6. **Vertex colours were washed out.** The static mesh builder sRGB-encodes vertex colours, which the
   materials read raw; they are now pre-linearised. This also fixed the pastel crowd and bales.
7. **Menu and HUD text overflowed.** Engine key names ("Gamepad Face Button Bottom") are now short
   (A / X / RT / D-pad up), text fits its panel, and line spacing uses the measured font height.
8. **Main-menu cursor** reset after visiting Options; the menu now remembers its position.
9. **Packaging:** UE 5.8's archive step copied the bare `Binaries/Mac` app, which has no content, so the
   game couldn't find its project and crashed during startup. `Tools/ue.sh package` now copies the
   staged app, which has the paks in `Contents/UE`, into `Build/Mac`.
10. **Bad dialog on first run:** the first game launch hit a blocking Xcode dialog ("missing Metal
   Toolchain"). macOS installs it on demand; the scripted runs now pass `-unattended` so a dialog
   can't stall them.

### Re-run after first play (2026-09-27)
The first person to play the packaged game found four problems:
1. **Full screen froze at launch.** Opened without arguments, the game used the engine's default
   (windowed full screen). The game thread then waited forever in `FMacWindow::WaitForFullScreenTransition`.
   Every earlier scripted run passed `-windowed`, so none hit it. The game now uses its own settings class
   (`UMXGameUserSettings`) and starts in a 1600×900 window, shrunk if the screen is smaller. The engine
   reads the window mode before any game code runs, so the values live in
   `Config/DefaultGameUserSettings.ini` under the new class's section. Settings saved by earlier builds,
   which asked for full screen, are ignored.
2. **The mouse was hidden and locked to the window**, so it was hard to leave the game. Unreal's
   defaults capture and lock the mouse on click, and the game hid the cursor everywhere. Now the mouse
   is never captured or locked (`Config/DefaultInput.ini`). The cursor shows on menus and hides during a
   race only while it rests over the game. The pause menu also gained QUIT GAME.
3. **"The AI uses turbo nonstop without overheating."** See §0: Easy and Medium now ride like people and
   sometimes overheat.
4. **A handled ensure** (`UVChannelData.bInitialized`) appeared once per run in every packaged run, the
   earlier ones included. It was missed then because it doesn't stop the game. Runtime-built meshes now set
   their UV channel data.

| Suite | Result |
|---|---|
| Compile | Succeeds, 0 errors, 0 warnings |
| Automation tests | 11/11 pass (new: `HeatlineMX.AI.TurboLikeAPerson`) |
| `loop` / `loopkb` / `designer` / `menus` (editor binary) | 24/24 / 24/24 / 15/15 / 7/7 |
| Packaged `loop` / `menus` | 24/24 / 7/7, and no ensure in either log |
| Packaged app opened with no arguments (`open Build/Mac/HeatlineMX.app`) | Reaches the main menu in about 7 s at 1600 wide, with no freeze and no ensure |

Not re-run this time: `perf` and `shots`. Nothing that affects rendering changed, apart from one more
line in the start-of-race controls reminder, which was checked in the 4-player screenshot.

Not verified, because it needs a person: how the real cursor looks and moves (scripted input never moves
the OS cursor), and whether Cmd+Q quits.
