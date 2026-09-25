# Known limitations

## Verification status
_Last updated 2026-09-25 (Unreal Engine 5.8.3 on macOS 26.6, M4 Max). See [TESTING.md](TESTING.md) for the numbers._

* **Compiled and run in the engine.** It compiles with no errors or warnings, and all 10 automation tests
  pass. Every scripted end-to-end scenario passes: 4-player loop, keyboard + 3 pads, Designer, menus, and
  camera framing in every layout.
* **Performance:** 4 local players + 4 AI at 1920×1080 run at 120 fps (the display's refresh) in both
  the editor binary and the packaged game. The packaged game's worst frame was 9.8 ms. Windowed Metal
  always syncs to the display, so an uncapped frame rate couldn't be shown on this machine.
* **Packaged build:** `Build/Mac/HeatlineMX.app` (Development, 835 MB) passes the loop and menus
  scenarios. It uses Unreal's default sandbox entitlements, so saves and records live in
  `~/Library/Containers/com.heatlinemx.HeatlineMX`. It is ad-hoc signed and not notarised, so on another
  Mac it needs right-click → Open the first time.
* **Physical controllers have not been tested.** All end-to-end runs use simulated devices through the
  real input routing. The two paired Xbox controllers need a person to run the checklist in TESTING.md §3.
* **Not yet tested:** real controller disconnects over Bluetooth (the autotest simulates the
  connection events), and any display other than this Mac's.

## Fidelity to the NES
* **Course data** (piece order, lanes, spacing, main-race-only pieces, laps) is decoded exactly from
  the ROM's course streams. The 3D shapes are smoothed from the NES per-column heights. The steep ramp
  is deliberately changed (see [DESIGN.md](DESIGN.md), "Scale").
* **Physics** keeps the NES mechanisms: turbo heat, a stall on overheat, cool strips on the ground
  only, pitch input changing the flight, landing windows, and the front-wheel barrier rule. The
  constants are rescaled for 3D, and several are changed for feel (DESIGN.md). It is a faithful
  *interpretation*, not a frame-exact port.
* **Not traced in the original code**, so our versions are designs, not reproductions:
  * crash roll distance and recovery timing
  * rival AI steering and pursuit
  * the ground in lanes 3–4 under the S platform
  * which ramp columns give the "downhill take-off" boost

  See [RESEARCH_ORIGINAL.md](RESEARCH_ORIGINAL.md) §9.
* **Progression:** the NES gates the main race behind a top-3 finish in the Challenge race, then
  moves to the next course. That gate is not reproduced. Championship scores points across the courses
  instead, and the Challenge layout is available as a separate variant.
* **Track Designer** is a superset of NES Design mode: cursor placement instead of driving the bike,
  lane choice, validation, undo/redo and local saves. The NES could not actually save; SAVE/LOAD were
  meant for the Famicom Data Recorder.
* **No turns or transitions** were added. The course is the straight NES course, unrolled, so the
  camera never rotates.

## Presentation
* All meshes are procedural (C++ mesh kit): the bike, the rider (primitive shapes with two-bone IK
  limbs, not a skinned character), crowd, props and track. There is no authored art.
* Engine and effect sounds come from a procedural synthesiser. There is no music, and no recorded
  samples.
* Rider animation is procedural: lean, tuck, IK to grips and pegs, and a crash tumble. There are no
  motion-captured clips.

## Scope (by design)
* Local play only: no online play or online track sharing.
* No tricks, weapons, upgrades or rider stats. Every bike has the same handling.
* At most 8 bikes per race: up to 4 humans plus AI.
