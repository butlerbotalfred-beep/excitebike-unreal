# Excitebike (NES, 1984): observed original mechanics

This file records only what the original game does. Modern changes are in [DESIGN.md](DESIGN.md).
Each fact is tagged with its source and confidence:

* **[CODE]**: reverse-engineered game logic. The main source is the byte-exact native physics port
  by eien86 ([ToolAssisted-run/exciteBot](https://github.com/ToolAssisted-run/exciteBot), MIT). It is
  validated frame-by-frame against emulator RAM dumps; see `docs/2d_engine_spec.md` and
  `source/engine.hpp` in that repo. The public bank-FF disassembly (cyneprepou4uk/NES-Games-Disassembly)
  is the other source. Both were read by streaming, and no ROM data is stored here.
* **[MANUAL]**: the Nintendo Classic Mini manual (CLV-P-NAAHE, linked in the brief) or the original
  1985 US manual (archive.org scan text).
* **[MAP]**: the nesmaps.com full-track maps, viewed in a browser.
* **[DERIVED]**: my own reading or arithmetic on the above. Treat it as less certain.

Units: the NES runs at 60 fps. Horizontal speed `velX` is 8.8 fixed-point (256 = 1 px/frame). One
track *column* is 8 px.

## 1. Controls
* A = accelerator. Releasing it brakes: "let go and the brakes operate". B = turbo. [MANUAL]
* D-pad Up/Down changes lane ("handlebar left/right"). Lane changes only work on the ground. [CODE]
* D-pad Left lifts the front wheel (nose up) and Right lowers it. This works on the ground (wheelie)
  and in the air. [CODE][MANUAL]
* The game has no jump button. Jumps come from the terrain. [CODE]

## 2. Speed
The speed/acceleration/friction step runs **once every 4 frames**. [CODE]

| Situation | Effect per 4 frames (velX units) | Cap |
|---|---|---|
| A held, below cap | +24 | 800 (3.125 px/f) |
| B held, below cap | +63 | 832 (3.25 px/f) |
| Coasting on the ground (no A/B) | −56 ("brakes") | – |
| A held above its cap | −12 | – |
| B held above its cap | 0 (speed kept) | – |
| Mud, B held / not held | −192 / −127 | – |
| Grass ("track missing") and the rough verges beyond the outer lanes | throttle ignored, −56 | – |
| Airborne, no tilt / nose-up / nose-down | −28 / **−60** / **0** | – |

* B's top speed is only **4 % higher** than A's. Turbo's real advantage is acceleration, which is
  2.6× stronger. [CODE]
* Some takeoff tiles are "downhill" launches (terrain type $13). Leaving one while on the ground adds
  +256 to velX, which pushes speed above the normal cap. The reference TAS reaches 2272 this way. [CODE]
* Speed calibration: a randomizer author measured 800/832 as about 80–83 km/h. [DERIVED from ExciteBikeRando README]

## 3. Engine temperature (turbo heat)
[CODE] The temperature byte runs from 8 (cold) to 32 (overheat). It steps by one unit whenever a
sub-counter overflows.

| Throttle | Equilibrium | Heating rate (sub-counter per frame) | Time for +1 unit |
|---|---|---|---|
| Coast | 8 | +63 | 4.1 f |
| A (or A+B) | 17 | +7 | 36.6 f |
| B | **32 = overheat** | +15 | 17.1 f |
| Above equilibrium (any throttle) | – | −11 | 23.3 f (cooling) |

* Holding B from cold overheats in about **6.8 s**. B's equilibrium equals the stall threshold, so
  continuous turbo always ends in a stall. [CODE][DERIVED]
* A settles at 17, which is 37.5 % of the heat range, and never overheats. [CODE][DERIVED]
* Warning at 26 or above (≈75 %) while holding B. [CODE, exciteBot memory notes]
* **Overheat:** the stall timer starts at the temperature (32) and drops one unit every 11 frames down
  to 8. That is 24 × 11 = 264 frames ≈ **4.4 s**. Throttle input is locked, the bike is stopped, and it
  drifts to the edge of the track. When the stall ends, the temperature resets to 5 (below cold).
  [CODE] The manual adds: "you will temporarily grind to a halt". [MANUAL]
* **Cool zones** (the `>>` arrows) instantly reset the temperature to 8 (cold), but only when the bike
  is on the ground and not stalled. Jumping over one does nothing. [CODE] Each arrow piece covers one
  lane (lane 1 or lane 4) and is 2 columns long. [CODE][MAP]
* In the air the throttle has no effect, and coasting still cools. The TAS uses its jumps as cooling
  time. [CODE]

## 4. Pitch, jumps and landings: how pitch changes the trajectory
The bike angle `0xAC` takes the values 2 to 12, with 6 as level. Higher means nose up. Each step is
roughly 15° [DERIVED: the wheelie flips at 13, which is 7 steps ≈ 105°].
* **On the ground,** the terrain sets the angle on ramps. On flat ground, Left raises the nose one step
  every 6 frames up to 10. Past 10 it keeps rising only while A/B is held, one step every 13 frames. At
  13 or more the bike **flips over (crash)**. Right lowers the nose back to level. [CODE]
* **In the air,** the angle changes one step every 4 frames, within the range 2 to 11. [CODE]

**Takeoff:** a ramp tile ($06) or a downhill tile ($13) launches the bike, but only above a minimum
speed. The initial vertical speed is **velX + 175**, so the launch depends on speed alone: every ramp
type launches at the same vertical speed for a given velX. Taller ramps give longer jumps only because
the arc starts higher. [CODE] The manual says: "The distance of a jump is determined by the speed
before jumping and the angle of the jump." [MANUAL]

**Airborne arc:** each frame a gravity accumulator adds a term selected by the *held D-pad input*, not
by the displayed angle. [CODE]

| Held in the air | Gravity term | Horizontal friction | Other | Net effect |
|---|---|---|---|---|
| nothing | 52 | −28 per 4 f | – | baseline arc |
| Left (nose up) | **24** (≈ half gravity) | **−60** per 4 f | – | **higher, floats longer, loses horizontal speed → shorter** |
| Right (nose down) | 52 | **0** | the whole vertical integration **skips every 4th frame** | same height, arc stretched in time by 4/3, no speed loss → **lower (than nose-up) but longer** |

The manual's technique text matches this: "Hold [Left] ... more elevation but less distance ... lower
the front wheel ... to jump lower but longer." [MANUAL] **So pitch input changes the trajectory
itself. The rotation you see on screen is not what does it; the held input changes gravity and
drag.** The angle only matters when the bike lands.

**Landing** is decided at first ground contact. [CODE] Each terrain angle belongs to a slope class 0–6:

| Slope class | Terrain angle(s) | Perfect angle | Safe window, fast (≥2 px/f) | Safe window, slow |
|---|---|---|---|---|
| 0 flat | 6 | 6 | 3–11 | 3–11 |
| 1 steep down | 3 | 3 | 2–8 | 2–9 |
| 2 down | 4–5 | 4 | 3–9 | 2–10 |
| 3 steepest down | 2 | 2 | 2–6 | 2–7 |
| 4 steep up | 10–11 | 11 | 9–11 | 8–11 |
| 5 up | 7–8 | 8 | 6–11 | 5–11 |
| 6 up | 9 | 9 | 8–11 | 7–11 |
| 7 | – | – | none (always crash) | none |

* Outside the window the bike **crashes**.
* On a slope, any angle inside the window is a clean landing.
* On flat ground, only the exact level angle (6) is clean. Any other safe angle gives a **wobble**: the
  bike re-launches with **half** the vertical speed (a bounce), gets 4 frames of braking friction, and
  the second touchdown is always clean. [CODE] The manual says to land "squarely on both wheels" for
  quick acceleration. [MANUAL]
* **Small obstacles** (terrain $07): a grounded bike crashes if its angle is below 7 (front wheel not
  lifted) and its speed is at least 2.5 px/f, which is 77 % of turbo top speed. Wheelie over them or
  slow down. "do a wheely (raise the front wheel in the air) to get over small obstacles". [CODE][MANUAL]

## 5. Lanes and track edges
* There are four lanes, 12 units apart. A lane change moves 1 unit per frame, so one lane takes
  **12 frames (0.2 s)**. A tap carries the bike all the way to the next lane. [CODE]
* The rough strip beyond the outer lanes brakes the bike and blocks acceleration. The bike bounces off
  the outer limit. [CODE] "Try to avoid rough areas as this will slow you down." [MANUAL]

## 6. Rivals, contact and crashes
* "you can make rivals fall off their bikes by nudging their front wheel with your back wheel." The
  rider who runs into the *rear* wheel of the rider ahead falls. [MANUAL]
* Rivals come in two types: "one type runs normally and the other type pursues". Rivals "try to avoid
  gaps in the track". [MANUAL]
* **Crash recovery:** "How long the fallen rider rolls depends on the speed he was going just before
  falling." Then the rider runs back to the bike, and the player "press[es] the A button over and over
  quickly until you return to the race". [MANUAL]
* Only the first four lanes are raced. Rivals appear only between two stream markers ($30/$31), and
  not on the final lap. [DERIVED from the disassembly; not fully traced]

## 7. Courses and race structure
* There are 5 courses with **2 laps** each. You first run a *Challenge* (qualifying) race and must
  place 3rd or better to reach the main race. Placing 3rd or better in the main race advances to the
  next course. Track 5 then repeats. [MANUAL][CODE]
* The main race adds extra pieces. These are stream bytes with bit 7 set, included only when RAM $46 = 1,
  which is set after qualifying. [CODE]
* The full per-course obstacle order, lanes, spacing and cool-zone placement are in
  [TRACK_MANIFEST.md](TRACK_MANIFEST.md). They were decoded from the course streams and checked
  against the maps. [CODE][MAP]
* The obstacle vocabulary is the 19 Design-mode pieces A–S, plus a built-in-only barrier variant ($03)
  and the finish deck ($09). [CODE][MANUAL]

## 8. Design mode
* Choose from 19 obstacles (A–S) and place them by driving the bike to a position. CL clears a piece
  and END finishes the lap. Set laps from 1 to 9. The limit is about 50 hurdles, depending on the
  pieces. [MANUAL]
* PLAY MODE A runs solo and PLAY MODE B runs against rivals. If you play before designing a track, the
  "track will run on forever". SAVE/LOAD do not work on the NES; they were meant for the Famicom Data
  Recorder. [MANUAL]

## 9. Things not verified (not claimed as faithful)
* The exact crash animation, roll distance and recovery timing formula. The manual confirms that roll
  length scales with speed and that mashing A speeds recovery; the exact numbers were not traced.
* Rival AI steering and pursuit rules, and rival-to-rival contact.
* How the ground behaves in lanes 3–4 under the S platform. It was read from tiles and the map, not
  traced in code.
* Which exact column of each ramp is the "downhill" ($13) takeoff. The TAS relies on these; the modern
  version replaces them with slope gravity (see DESIGN.md).
