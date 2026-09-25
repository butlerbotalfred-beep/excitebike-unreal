# Heatline MX: design decisions (modern adaptations)

Heatline MX is a modern take on NES Excitebike, built in Unreal Engine with C++. Its core is lanes,
ramps, pitch, landings and turbo heat. [RESEARCH_ORIGINAL.md](RESEARCH_ORIGINAL.md) lists what the
original does. This file lists every place we **deliberately** differ from it, and why. Every number
here is a default in a Data Asset (`DA_BikeTuning`, `DA_CameraTuning`, `DA_AI_*`); see
[TUNING.md](TUNING.md).

## Scale
* One NES column (8 px) becomes **1.25 m** of track. One NES height row becomes **0.6 m**.
* Speeds use the same horizontal scale, so the *time* between obstacles matches the NES. At the
  original turbo cap of 3.25 px/f this gives 30.5 m/s.
* Lanes are 3.2 m wide, which fits side-by-side 3D bikes. The original lanes are about 1.9 m at this
  scale.
* Ramp and deck shapes follow the NES per-column heights ([TRACK_MANIFEST.md](TRACK_MANIFEST.md)),
  smoothed into straight faces. One is changed on purpose. On the NES the steep ramp (E, `$0B`,
  heights 2-4-5-4-2) starts with a 2-row step. In 3D that step launched the bike at over 60° with a
  9.5 m apex, so here it is a plain 5-row peak. All take-offs are also capped at 36°
  (`MaxLaunchAngle`); speed is kept, only the direction is limited.

## Track structure and camera
* The course is **straight and unrolled for the whole race distance**. Lap 2 is lap 1's obstacle
  sequence placed again after the finish deck, exactly as the NES scrolls. We added **no turns or
  transitions**, so the camera never rotates or cuts and the race always moves toward screen-right.
  The only additions are a start grid and a run-out after the final finish line; the manifest labels
  both.
* Default camera: elevated three-quarter side-follow on the near side of the track. It does not chase
  a look-at point. It has a fixed orientation (pitched down about 18°, turned about 27° towards the
  direction of travel) and sits back along the line of sight that puts the bike at a set screen
  position: about 30 % in from the left and a little below centre. It slides a few percent further left
  at top speed to show more track ahead. So the bike holds the same spot in every view shape, and the
  camera never rotates or cuts. It follows the bike's lane and height only partly, so lane changes stay
  readable and the landing zone stays in frame on big jumps.
* The camera is tuned separately for full screen, 2P top/bottom, 2P side-by-side and the quarter view,
  via `DA_CameraTuning`: distance, field of view, pitch, yaw, screen position and follow amounts. The
  `shots` autotest checks every player view: the bike is framed, and the rider's own lane stays visible
  at least 1.0 s ahead at top speed (measured 1.01–1.51 s; the NES shows about 1 s ahead of the bike).

## Bike controller
* Movement is a **deterministic kinematic controller in track space**: distance along the track `s`,
  lateral offset `y`, height `z`, speed and pitch. It steps at a fixed 120 Hz, and the visuals
  interpolate between steps. There are no free rigid bodies, so the bike can't randomly fall over.
  Suspension compression, wheel spin, rider lean and crash tumbles are a visual layer driven by the
  simulation state.
* Ground and physics truth come from the **track model**: analytic height, slope and surface per lane,
  built from the same obstacle definitions that generate the meshes. What you see is exactly what
  you ride.
* Throttle (normal) and turbo are separate buttons, like the NES. Releasing both engine-brakes firmly,
  as the NES "brakes", though less violently. This matters because controlling speed before a jump
  is a skill.
* **Turbo** adds more speed than on the NES: a +12 % cap instead of +4 %, and about 2.3× the
  acceleration. It heats at the NES rate, overheating in about 6.8 s from cold. Normal throttle
  settles at 37.5 % heat, as the NES does. Cooling above equilibrium runs at the NES rate.
* **Overheat** stalls the bike for **3.5 s** (NES: 4.4 s), shortened for quicker retries. The stalled
  bike rolls to a stop in its lane with collisions off, so nobody gets an unavoidable crash from it.
  Heat resets to 0 afterwards.
* **Cool strips** instantly reset heat to 0, but only while on the ground, as on the NES. Jumping over
  one gets you nothing.
* **Mud** caps speed at 55 % (45 % while turbo is held; turbo digs in, as on the NES). Grass
  ("track missing") and the verges cap speed at 50 % and disable turbo.
* **Slopes:** gravity along the surface speeds you up downhill and slows you down uphill. The NES used
  discrete "downhill takeoff" +256 boosts instead. **Flow boost:** a clean landing on a downslope adds
  up to +8 % speed, above the throttle cap. Holding turbo keeps it; normal throttle and coasting bleed
  it off. This recreates the NES over-cap speed from downhill sections as a skill reward.

### Pitch and the jump trajectory (explicit decision)
On the NES, pitch *input* changes the arc: gravity, drag, and vertical time-stretch. The displayed
angle only decides the landing. **We keep that split. Rotating the bike alone does not change the
flight; your input does, and the rotation that comes with it decides the landing.**

| In the air | Rotation | Flight effect (default tuning) | NES counterpart |
|---|---|---|---|
| Pull back (nose up) | +150°/s, max +75° | gravity × 0.50 and air drag 12 m/s²: **higher and floatier, but shorter** | gravity term 24 instead of 52, friction 60 |
| Neutral | holds angle (the optional Landing Assist eases toward the landing slope at 25°/s) | gravity 40 m/s², air drag 4 m/s² | friction 28 (the NES penalty is much harsher) |
| Push forward (nose down) | −150°/s, max −60° | no drag, vertical motion at 0.75× time rate: **same apex, 1.33× airtime, longer and faster** | vertical update skips every 4th frame, friction 0 |

Analog input scales both the rotation rate and the flight effect. Effects blend in over 0.1 s so they
stay in step with the visible rotation and the rider's lean back or tuck forward. Because the same
input rotates the bike, there is a trade-off: forward carries you farther but tips the nose down, so
you must bring the angle back before touchdown. That is the NES tension between Left and Right.

**Launch** comes from geometry: speed along the lip direction when the bike leaves a convex crest or
drop. That makes jump height depend on both speed and ramp shape. On the NES every ramp launches with
the same vertical speed for a given velocity. Rider input at the lip is a modern addition: pulling
back in the last 0.15 s "pops" +12 % vertical speed, pushing forward "scrubs" −20 %. Slow bikes roll
over small crests instead of launching.

**Landing** compares bike pitch with the surface under the wheels (Δ = pitch − surface angle):
perfect ±7°, clean −15°…+20°, wobble −35°…+50°, crash outside that. The window is tighter on
upslopes, where a nose-first landing digs in, and looser nose-high on downslopes. This follows the
NES slope tables: very forgiving on flat ground, unforgiving nose-down on up-faces.
* Perfect keeps all speed, plus flow boost on downslopes. Clean costs 4 %.
* A **wobble** is the NES wobble: a small bounce at 35 % of the impact speed, 18 % speed loss and
  0.4 s without drive.
* A hard flat landing (impact normal to the surface above 16 m/s) costs extra speed. That makes
  "land on the downslope" the natural motocross skill.

**Wheelies:** pull back on the ground to lift the front to about 38°. Keep holding with throttle and it
climbs further; it loops out at 72°, like the NES flip at angle 13. **Barriers** crash you if you hit
them with the front wheel down (below 12°) above 75 % of top speed. That is the NES rule: under
angle 7 and at 77 % speed or more.

### Lanes and air steering
* Classic scheme: up/down *taps* move exactly one lane (0.28 s). Holding keeps moving, and the bike
  settles on lane centres. Modern scheme: analog steering across the track with light lane
  magnetism.
* **Air steering is limited to 20 %** of ground lateral speed; the NES allows none. That is enough to
  correct a line, not enough to pick a new lane in the air. Lane choice before a jump still matters.
* Tall obstacles block lateral movement where they would be a wall, such as the side of the S
  platform deck. Riding off an edge makes you airborne.

## Rider contact (tactical role kept, chains prevented)
* Contact happens in the same lane band, front wheel against rear wheel. If the rear rider closes in
  faster than 3 m/s, **the rear rider crashes**. That is the NES rule: "nudge their front wheel with
  your back wheel". At a lower closing speed the rear rider is just bumped to the leader's speed.
* **Cut-off rule:** if the rider ahead moved sideways into the lane during the last 0.5 s, the rear
  rider only wobbles (−25 % speed). Cutting in front still hurts your rival, but it can't cause an
  unavoidable crash.
* **Landing rule:** if either rider landed less than 0.5 s ago, the rear rider only wobbles and is held
  behind. A rider in the air can't brake. A rider who lands just ahead may lose speed on the landing
  before anyone could react. (`ContactReactTime`; in the 8-bike offline races this cut AI contact
  crashes from about 2.2 to 0.5 per race.)
* Side-by-side contact pushes both bikes apart and costs 3 %. It never causes a crash.
* **Protection:** a remounted rider is ghosted (no collisions) for 2 s. Crashed and overheated bikes
  never collide. A rider can be crashed by contact at most once every 6 s; further contact inside that
  window is only a wobble.

## Crashes and recovery
* A crash is triggered by a landing outside the window, a barrier hit, a wheelie loop-out, rear-wheel
  contact, or hitting a near-vertical face.
* The rider tumbles for 0.6 s plus up to 0.8 s, scaled by speed (the NES rolls farther when faster),
  then runs back. Recovery is 1.4 s, and **each press of Accelerate cuts 0.12 s, down to 0.5 s**. That
  matches the manual's "press the A button over and over".
* **Resetting never gains progress.** The bike is placed on the nearest flat, obstacle-free ground *at
  or before* the crash point, and in a free lane. The tumble slide is only visual. Checkpoint progress
  is capped at the crash point.

## Race rules
* Checkpoints: every lap has ordered gates, about one every 60 m, plus the finish line. Progress only
  counts through gates crossed in order and on the correct lap. The finish counts only when crossed
  forward with every gate of that lap passed. Movement can't go backwards, but the rule is enforced
  anyway, and recovery placement is clamped.
* The winner is decided by the **validated finishing order**. Time-trial medals (gold/silver/bronze
  target times per course) are kept separate from race position. The medals are a modern addition;
  the NES has no medals. Their targets are *measured*, not estimated: 1.03 / 1.10 / 1.20 × the time
  of a reference rider (Hard AI skill, no random mistakes, same physics) on that exact layout and lap
  count. So they stay right for custom lap counts and for Designer tracks.
* The NES layouts come in two variants. **Challenge** (qualifier) is the default for Time Trial.
  **Main** includes the main-race-only pieces and is used by races, Versus and Championship.

## Modes
* **Time Trial:** solo, local best times, and a translucent ghost of your personal best. Medals are
  stored per course and variant.
* **Race vs AI:** 1 human against 5 AI riders.
* **Local Versus:** 2–4 humans, with 0–4 optional AI (8 bikes at most).
* **Championship:** the five NES courses in order (main layouts), with points 10-8-6-5-4-3-2-1 and standings
  between races.
* **Track Designer:** see below.

## AI
AI riders drive the same movement component through the same input struct players use. They get no
speed boosts; difficulty only changes skill:

| | Easy | Medium | Hard |
|---|---|---|---|
| Reaction delay | 0.35 s | 0.2 s | 0.1 s |
| Landing angle error (σ) | 14° | 7° | 3° |
| Heat management | turbo off at 60 %, back on at 35 % | 80 % / 65 % | 94 % / 90 %, plans cool strips |
| Lane planning horizon | 25 m | 45 m | 70 m |
| Air plans (rollouts) | no, just lines up | yes | yes |
| Recovery mash rate | 4/s | 7/s | 10/s |

AI rivals come in two personalities, like the NES: **cruisers** hold good lines, while **chasers**
target the nearest human and try to cut in ahead of them (only at Medium and Hard).

The pitch input changes the flight, so an air plan that looks good with a fixed input can be
impossible to line up for the landing in time. Medium and Hard therefore choose between neutral,
nose-down and nose-up plans by flying each one with the real bike physics and their own in-flight
controller, then keep the plan that gets furthest without crashing. Barrier wheelies use a partial
lift that clears the front-wheel rule without over-rotating. Weaker riders sometimes get greedy and
ride turbo into the red (Easy most often, Hard almost never). The offline benchmark in
[TESTING.md](TESTING.md) confirms Hard > Medium > Easy on every course and layout.

## Local multiplayer
* This uses Unreal's native local players (`UGameInstance::CreateLocalPlayer`) with split screen. There
  are no network clients. Layouts:
  * 1P: full screen.
  * 2P: top/bottom by default, side-by-side as an option.
  * 3P: three quadrants, with live standings drawn in the fourth by the viewport client.
  * 4P: a 2×2 grid.
* Device routing: a custom `UGameViewportClient` sends each input device (the keyboard, or a
  gamepad's `FInputDeviceId`) to the local player that joined with it. Enhanced Input mapping contexts
  then handle bindings per player. Four gamepads, or one keyboard plus three gamepads, both work.
* In the lobby, press A (or Enter) on any device to join and B (or Esc) to leave. Left/right picks a
  rider colour; colours are unique. Pressing A toggles ready. If a controller disconnects, that
  player's race pauses with a prompt. Reconnect it, or press A on a free controller to take the slot
  over.

## Controls
There are two remappable presets, stored per player:
* **Classic** (screen-relative): Up/Down change lane, Left/Right = nose up/down.
* **Modern** (rider-relative): stick Left/Right steers across the track (left = toward the far side),
  stick Forward/Back = nose down/up.

Accelerate and Turbo are separate buttons in both. See [CONTROLS.md](CONTROLS.md).

## Track Designer
You work in the side view with a cursor on the lap. Pick a piece from the library (the A–S
equivalents plus modern variants) and place it at the cursor. You can move or delete pieces, choose
lanes for lane-specific pieces, add cool strips and grass or rough sections, set the lap length
(150–1500 m) and the laps (1–9), and use undo and redo. Tracks save and load by name as local JSON.
The validator flags overlapping pieces, pieces past the lap end, missing run-up before a ramp, blocked
landings and a missing finish. You can test-ride instantly, and any saved track is selectable for
split-screen races. Built-in courses and user tracks share the same `FMXTrackDefinition`. No online
sharing.
