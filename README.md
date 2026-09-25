# Heatline MX

A modern interpretation of NES **Excitebike** (1984) in Unreal Engine 5. It's built around lanes,
ramps, bike pitch, landings and turbo heat, for 1–4 local players in split screen.

* **Courses:** all five NES courses, translated from the original data
  ([docs/TRACK_MANIFEST.md](docs/TRACK_MANIFEST.md)), plus a handling test strip.
* **Modes:** Time Trial (records, medals, personal-best ghost), Race vs AI, Local Versus (2–4 players
  plus optional AI), Championship, and a Track Designer.
* **Engine code:** C++ for the bike controller, race rules, local players, AI and track data.
  Tuning, presentation and course assembly live in Data Assets and Blueprints.

## Documentation
| File | Contents |
|---|---|
| [docs/RESEARCH_ORIGINAL.md](docs/RESEARCH_ORIGINAL.md) | What the NES game does, with sources and confidence |
| [docs/DESIGN.md](docs/DESIGN.md) | Every deliberate modern adaptation, including the pitch-and-trajectory decision |
| [docs/TRACK_MANIFEST.md](docs/TRACK_MANIFEST.md) | Source-to-modern track manifest with stable segment IDs |
| [docs/CONTROLS.md](docs/CONTROLS.md) | Classic and Modern presets, keyboard and gamepad |
| [docs/TUNING.md](docs/TUNING.md) | Every tuning value and where it comes from |
| [docs/TESTING.md](docs/TESTING.md) | Automated tests, simulated-input runs, physical-controller checklist, performance results |
| [docs/ASSET_PROVENANCE.md](docs/ASSET_PROVENANCE.md) | Where every asset comes from (all procedural or engine-provided) |
| [docs/LIMITATIONS.md](docs/LIMITATIONS.md) | Known limitations |

## Status (2026-09-25)
The game builds and runs on Unreal Engine 5.8.3 on macOS. A packaged Mac app is at
`Build/Mac/HeatlineMX.app` (double-click to play). All automation tests and scripted end-to-end
scenarios pass, and 4 players + 4 AI hold 120 fps at 1080p on an M4 Max. The one check still owed is a
person playing with the real controllers ([docs/TESTING.md](docs/TESTING.md) §3). A 10-second
4-player clip is in [docs/media/heatline_4p_demo.mp4](docs/media/heatline_4p_demo.mp4). Record a new one with
`Tools/ue.sh autotest demo -benchmark -fps=30 -mxdemoframes=300`, then encode the frames with ffmpeg.

## Build and run (macOS)
```bash
Tools/ue.sh build        # compile HeatlineMXEditor
Tools/ue.sh content      # create materials, Data Assets, Blueprints, map (editor Python)
Tools/ue.sh tests        # headless automation tests
Tools/ue.sh game         # play (standalone window)
Tools/ue.sh autotest loop   # scripted end-to-end run with simulated input
Tools/ue.sh package      # packaged Mac app into Build/Mac/HeatlineMX.app
Tools/ue.sh packaged-autotest loop   # scripted run against the packaged app
Tools/offline_check/run.sh   # engine-free check of the gameplay simulation (clang only)
```
To regenerate the course data from the public disassembly, run
`python3 Tools/nes_manifest/build_manifest.py`.

## Source layout
```
Source/HeatlineMX/
  Core/      types, tuning Data Asset classes
  Track/     track definition + JSON I/O, obstacle library, track model (physics truth), validator, course library, track actor
  Bike/      deterministic bike simulation (FMXBikeSim), bike pawn with procedural bike/rider + animation
  Race/      race manager (fixed step, checkpoints, standings, ghosts), contact rules, game mode, player controller + camera, environment
  AI/        AI brain (same input struct and physics as players)
  Session/   game instance, save game, Enhanced Input config, viewport client (device routing, split layouts)
  UI/        HUD, front end menus, canvas helpers
  Designer/  in-game Track Designer
  Audio/     procedural engine and SFX synthesiser
  FX/        mesh kit, materials registry, particle manager
  Dev/       autotest harness
  Tests/     automation tests
```
