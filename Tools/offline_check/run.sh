#!/bin/bash
# Engine-free check of the Heatline MX gameplay layer.
# Compiles the pure-logic C++ (types, tuning, obstacles, track model, validator, bike sim, AI, contact)
# against tiny UE shims with clang, then runs:
#   run.sh             automation-test equivalents + NES course validation, AI runs and drive tests
#   run.sh ai [N] [trace]              N-seed AI benchmark per course/layout/difficulty (default 8)
#   run.sh trace C V D SEED S0 S1      step trace of one AI run (course 1-5, variant 0/1, difficulty 0-2)
# This does not replace the UE automation tests; it exists so the simulation can be checked without the editor.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SRC="$ROOT/Source/HeatlineMX"
OUT="$ROOT/Saved/OfflineCheck"
mkdir -p "$OUT"

# Flatten the course JSON into the simple text format the harness reads.
python3 - "$ROOT/Content/Courses" "$OUT/courses.txt" <<'PY'
import json, os, sys
src, dst = sys.argv[1], sys.argv[2]
with open(dst, "w") as f:
    for n in range(1, 6):
        c = json.load(open(os.path.join(src, f"nes_t{n}.json")))
        f.write(f"COURSE {c['id']} {c.get('laps', 2)} {c['lapLengthM']} {c.get('nesTrack', n)}\n")
        for s in sorted(c["segments"], key=lambda s: s["startM"]):
            mask = sum(1 << (l - 1) for l in s.get("lanes", [1, 2, 3, 4]))
            runs = s.get("runs") or []
            f.write(f"SEG {s['id']} {s['type']} {mask} {s['startM']} {s['lengthM']} {1 if s['variant'] == 'main' else 0} {len(runs)} {' '.join(map(str, runs))}\n")
PY

clang++ -std=c++20 -O2 -w -I "$HERE/shim" -I "$SRC" -o "$OUT/offline_check" "$HERE/main.cpp" \
  "$SRC/Core/MXTypes.cpp" "$SRC/Core/MXTuning.cpp" "$SRC/Track/MXObstacleLibrary.cpp" "$SRC/Track/MXTrackModel.cpp" \
  "$SRC/Track/MXTrackValidator.cpp" "$SRC/Bike/MXBikeSim.cpp" "$SRC/AI/MXAIBrain.cpp" "$SRC/Race/MXContact.cpp" "$SRC/Race/MXProgress.cpp"
cd "$OUT" && ./offline_check "$@"
