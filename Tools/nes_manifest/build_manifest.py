#!/usr/bin/env python3
"""Build the source-to-modern track manifest for Heatline MX.

Streams the public Excitebike (NES) disassembly (cyneprepou4uk/NES-Games-Disassembly,
Excitebike/bank_FF.asm) from GitHub, decodes the five course streams and the obstacle-piece
geometry in memory, and writes only DERIVED data:

  Content/Courses/nes_t{1..5}.json   modern course definitions (+ per-segment NES source refs)
  docs/TRACK_MANIFEST.md             human-readable manifest

Nothing from the ROM/disassembly is written to disk verbatim.

Usage:
  python3 Tools/nes_manifest/build_manifest.py            # stream from GitHub
  python3 Tools/nes_manifest/build_manifest.py --stdin     # read the .asm from stdin
"""
import json, os, re, sys, urllib.request

ASM_URL = "https://raw.githubusercontent.com/cyneprepou4uk/NES-Games-Disassembly/main/Excitebike/bank_FF.asm"
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Modern track scale. One NES track column (8 px) -> METERS_PER_COLUMN metres of track.
# Speeds are scaled by the same factor, so NES spacing *timing* is preserved.
METERS_PER_COLUMN = 1.25

# ---------------------------------------------------------------------------------------------
# Piece classification (decoded from piece tile geometry + verified against nesmaps.com maps).
# letter = Design-mode letter (A-S) where one exists; lanes use 1 = far (top of NES screen).
# ---------------------------------------------------------------------------------------------
PIECES = {
    0x08: dict(letter="A", type="RampSmall",   lanes=[1, 2, 3, 4], label="small ramp"),
    0x07: dict(letter="B", type="RampMedium",  lanes=[1, 2, 3, 4], label="medium ramp"),
    0x05: dict(letter="C", type="RampLarge",   lanes=[1, 2, 3, 4], label="large ramp"),
    0x01: dict(letter="D", type="TableLow",    lanes=[1, 2, 3, 4], label="low table-top"),
    0x0B: dict(letter="E", type="RampSteep",   lanes=[1, 2, 3, 4], label="steep ramp"),
    0x06: dict(letter="F", type="RampSteepBack", lanes=[1, 2, 3, 4], label="large ramp, steep back"),
    0x0A: dict(letter="G", type="RampSteepFace", lanes=[1, 2, 3, 4], label="large ramp, steep face"),
    0x0E: dict(letter="H", type="Kicker",      lanes=[1, 2, 3, 4], label="kicker (jump ramp with sheer drop)"),
    0x02: dict(letter="I", type="Barrier",     lanes=[1, 2],       label="small barrier, lanes 1-2"),
    0x03: dict(letter="-", type="Barrier",     lanes=[1, 2],       label="small barrier, lanes 1-2 (built-in variant of I)"),
    0x04: dict(letter="J", type="Barrier",     lanes=[3, 4],       label="small barrier, lanes 3-4"),
    0x0C: dict(letter="K", type="Mud",         lanes=[1, 3],       label="mud, lanes 1 & 3"),
    0x0D: dict(letter="L", type="Mud",         lanes=[2, 4],       label="mud, lanes 2 & 4"),
    0x0F: dict(letter="M", type="CoolStrip",   lanes=[1],          label="cool zone, lane 1"),
    0x10: dict(letter="N", type="CoolStrip",   lanes=[4],          label="cool zone, lane 4"),
    0x12: dict(letter="O", type="Grass",       lanes=[3, 4],       label="track missing (grass), lanes 3-4"),
    0x13: dict(letter="P", type="Grass",       lanes=[1, 2],       label="track missing (grass), lanes 1-2"),
    0x11: dict(letter="Q", type="Grass",       lanes=[1, 2, 3, 4], label="track missing (grass), all lanes"),
    0x15: dict(letter="R", type="Mountain",    lanes=[1, 2, 3, 4], label="mountain (two-step mesa)"),
    0x14: dict(letter="S", type="PlatformDeck", lanes=[1, 2, 3, 4], label="ramp and platform jump"),
    0x09: dict(letter="-", type="FinishDeck",  lanes=[1, 2, 3, 4], label="finish deck (lap line)"),
}
# Composite pieces: start piece -> (run block tables / end pieces) are folded into one segment.
COMPOSITE_END = {0x11: {0x17}, 0x12: {0x19}, 0x13: {0x1B}}
RAMP_TYPES = {"RampSmall", "RampMedium", "RampLarge", "TableLow", "RampSteep", "RampSteepBack",
              "RampSteepFace", "Kicker", "Mountain", "PlatformDeck"}


def load_asm():
    if "--stdin" in sys.argv:
        return sys.stdin.read().splitlines()
    with urllib.request.urlopen(ASM_URL, timeout=60) as r:
        return r.read().decode("utf-8", "replace").splitlines()


def parse_memory(lines):
    mem = {}
    for line in lines:
        m = re.search(r'00:([0-9A-F]{4}):\s+([0-9A-F]{2})\s+\.byte', line)
        if m:
            mem[int(m.group(1), 16)] = int(m.group(2), 16)
            continue
        m = re.search(r'00:([0-9A-F]{4}):\s+((?:[0-9A-F]{2} ){1,3})', line)
        if m:
            a = int(m.group(1), 16)
            for i, b in enumerate(m.group(2).split()):
                mem.setdefault(a + i, int(b, 16))
    return mem


def decode(mem):
    rd = mem.get
    widths = {}
    rows = {}
    for p in range(0x24):
        ptr = rd(0xF063 + p) | (rd(0xF087 + p) << 8)
        a, cols = ptr, []
        while True:
            s = rd(a)
            if s is None or s == 0:
                break
            cols.append(s)
            a += 1 + (15 - s)
        widths[p] = len(cols)
        rows[p] = cols
    block = [rd(0xF4C6 + i) for i in range(7)]
    tracks = []
    for t in range(5):
        ptr = rd(0xED3A + t) | (rd(0xED40 + t) << 8)
        data, a = [rd(ptr)], ptr + 1
        while True:
            b = rd(a); data.append(b); a += 1
            if (b & 0x40) and not (b & 0x80):
                data.append(rd(a)); a += 1
                continue
            if (b & 0x3F) == 0x09:
                break
        tracks.append(data)
    return widths, rows, block, tracks


def build_segments(data, widths, block):
    """Walk one course stream (main-race layout: all pieces placed) and emit segments."""
    segs, col, i = [], 0, 1
    markers = []
    pending = None  # composite in progress
    while i < len(data):
        b = data[i]
        if (b & 0x40) and not (b & 0x80):
            k, n = b & 0x0F, data[i + 1] & 0x7F
            w = n * widths[block[k]]
            if pending is not None:
                pending["runs"].append(n)
                pending["widthCols"] += w
            col += w
            i += 2
            continue
        main_only = bool(b & 0x80)
        pid = b & 0x3F
        i += 1
        if pid >= 0x30:
            markers.append(dict(col=col, marker=pid))
            continue
        w = widths[pid]
        if pending is not None:
            pending["widthCols"] += w
            pending["pieces"].append(pid)
            if pid in pending.get("endSet", set()) or (pending["pid"] in (0x14, 0x15) and pid in (0x1B, 0x23)):
                segs.append(pending)
                pending = None
            col += w
            continue
        seg = dict(pid=pid, col=col, widthCols=w, mainOnly=main_only, runs=[], pieces=[pid])
        if pid in COMPOSITE_END:
            seg["endSet"] = COMPOSITE_END[pid]
            pending = seg
        elif pid in (0x14, 0x15):
            pending = seg
        else:
            segs.append(seg)
        col += w
        if pid == 0x09:
            break
    for s in segs:
        s.pop("endSet", None)
    return segs, col, markers


def lap_length_cols(segs, total_cols, qualifier):
    if not qualifier:
        return total_cols
    return total_cols - sum(s["widthCols"] for s in segs if s["mainOnly"])


def detect_combos(tid, segs):
    """Label the important obstacle combinations (rhythm sections, gap jumps, chicanes...)."""
    combos = []
    def ramp(s):
        return PIECES[s["pid"]]["type"] in RAMP_TYPES
    n, k = len(segs), 0
    while k < n:
        # rhythm: >= 3 ramps separated by <= 3 columns
        if ramp(segs[k]):
            j = k
            while j + 1 < n and ramp(segs[j + 1]) and segs[j + 1]["col"] - (segs[j]["col"] + segs[j]["widthCols"]) <= 3:
                j += 1
            if j - k + 1 >= 2:
                kinds = [PIECES[segs[x]["pid"]]["letter"] for x in range(k, j + 1)]
                combos.append(dict(segments=[segs[x]["id"] for x in range(k, j + 1)],
                                   label=f"{'rhythm section' if j - k + 1 >= 3 else 'double'}: {'-'.join(kinds)}"))
            k = j + 1
            continue
        k += 1
    for a, b in zip(segs, segs[1:]):
        ta, tb = PIECES[a["pid"]]["type"], PIECES[b["pid"]]["type"]
        gap = b["col"] - (a["col"] + a["widthCols"])
        if ta in RAMP_TYPES and tb == "Grass" and gap <= 4:
            lanes = PIECES[b["pid"]]["lanes"]
            combos.append(dict(segments=[a["id"], b["id"]],
                               label=f"jump the grass: {PIECES[a['pid']]['letter']} launches over {'full-width' if len(lanes) == 4 else 'half-track'} grass"))
        if ta == "Grass" and tb == "Grass" and gap <= 8:
            combos.append(dict(segments=[a["id"], b["id"]], label="grass chicane: forced lane swap"))
        if ta == "CoolStrip" and tb in RAMP_TYPES and gap <= 3:
            combos.append(dict(segments=[a["id"], b["id"]], label="cool strip right before a ramp: stay grounded in its lane"))
    # mud / barrier slaloms
    run = []
    for s in segs + [None]:
        if s is not None and PIECES[s["pid"]]["type"] in ("Mud", "Barrier"):
            if run and s["col"] - (run[-1]["col"] + run[-1]["widthCols"]) > 12:
                if len(run) >= 3:
                    combos.append(dict(segments=[x["id"] for x in run], label=f"slalom: {len(run)} mud/barrier pieces alternating lanes"))
                run = []
            run.append(s)
        else:
            if len(run) >= 3:
                combos.append(dict(segments=[x["id"] for x in run], label=f"slalom: {len(run)} mud/barrier pieces alternating lanes"))
            run = []
    for i, c in enumerate(combos, 1):
        c["id"] = f"T{tid}.C{i:02d}"
    return combos


def main():
    mem = parse_memory(load_asm())
    widths, rows, block, tracks = decode(mem)
    md = []
    md.append("# Source-to-modern track manifest\n")
    md.append("Generated by `Tools/nes_manifest/build_manifest.py`. It streams the public Excitebike (NES) bank-FF "
              "disassembly (cyneprepou4uk/NES-Games-Disassembly), decodes the course streams in memory and writes "
              "only this derived description. The decoded positions were checked against the nesmaps.com track "
              "maps. For example, Track 1's first mud, large ramp, medium ramp and cool zone all land where the "
              "decode predicts.\n")
    md.append(f"* **Scale:** one NES track column (8 px) = **{METERS_PER_COLUMN} m** of modern track. Speeds use the same scale, so the time between obstacles matches the original.")
    md.append("* **Lanes:** lane 1 is the far lane (top of the NES screen), lane 4 the near lane.")
    md.append("* **Main-race pieces:** a *main* segment is flagged in the ROM (bit 7) and appears only in the main race after qualifying. The *Challenge* (qualifier) layout omits it. Race modes use the main layout; Time Trial defaults to the Challenge layout.")
    md.append("* **Laps:** every NES course runs **2 laps** (first stream byte). Each lap ends on the finish deck.")
    md.append("* **Markers:** stream bytes $30/$31 (not terrain) set RAM $03A8, which appears to gate the rival-spawn slot. This is inferred and not fully traced. They are recorded as markers and are not used by the modern game.")
    md.append("* **IDs:** segment IDs `T<track>.O<nn>` follow NES stream order and stay stable. Combination IDs `T<track>.C<nn>` group segments.\n")
    md.append("## Obstacle vocabulary\n")
    md.append("| NES piece | Design letter | Modern type | Lanes | Width (cols) | Height profile (rows above track, per column) |")
    md.append("|---|---|---|---|---|---|")
    for pid, info in sorted(PIECES.items()):
        prof = [9 - r for r in rows[pid]]
        md.append(f"| ${pid:02X} | {info['letter']} | {info['type']} | {','.join(map(str, info['lanes']))} | {widths[pid]} | {prof} |")
    md.append("\nThe composite pieces (O/P/Q grass, R mountain, S platform) also carry run-length parameters from the stream, listed per segment below. S (platform) is a full-width ramp up to a deck about 6 rows high. Past the ramp top, the deck continues only over lanes 1–2 and ends in a sheer drop. Lanes 3–4 slope back down to the ground, where mud patches sit under the deck. The exact ground behaviour in lanes 3–4 was read from the map and tiles, not traced in code.\n")

    for t, data in enumerate(tracks):
        tid = t + 1
        segs, total_cols, markers = build_segments(data, widths, block)
        for i, s in enumerate(segs, 1):
            s["id"] = f"T{tid}.O{i:02d}"
        combos = detect_combos(tid, segs)
        q_cols = lap_length_cols(segs, total_cols, True)
        course = dict(
            schema="heatline.course/1",
            id=f"nes-t{tid}",
            name=f"Course {tid}",
            source=dict(game="Excitebike (NES, 1984)", track=tid, laps=data[0], lapColumnsMain=total_cols,
                        lapColumnsChallenge=q_cols, method="decoded from public disassembly stream; positions checked against nesmaps.com"),
            metersPerColumn=METERS_PER_COLUMN,
            laps=data[0],
            lapLengthM=round(total_cols * METERS_PER_COLUMN, 2),
            segments=[],
            combos=combos,
            markers=[dict(col=m["col"], atM=round(m["col"] * METERS_PER_COLUMN, 2), marker=f"${m['marker']:02X}") for m in markers],
            additions=[dict(id=f"T{tid}.X01", type="StartGrid", atM=0.0, note="modern start grid (two rows of four); NES starts all riders at column 0"),
                       dict(id=f"T{tid}.X02", type="RunOut", note="modern run-out after the final finish line so bikes can slow down; not part of any lap")],
        )
        for s in segs:
            info = PIECES[s["pid"]]
            seg = dict(id=s["id"], type=info["type"], lanes=info["lanes"],
                       startM=round(s["col"] * METERS_PER_COLUMN, 3),
                       lengthM=round(s["widthCols"] * METERS_PER_COLUMN, 3),
                       variant="main" if s["mainOnly"] else "both",
                       nes=dict(piece=f"${s['pid']:02X}", letter=info["letter"], col=s["col"], widthCols=s["widthCols"]))
            if s["runs"]:
                seg["nes"]["runs"] = s["runs"]
                seg["runs"] = s["runs"]
            course["segments"].append(seg)
        os.makedirs(os.path.join(ROOT, "Content", "Courses"), exist_ok=True)
        with open(os.path.join(ROOT, "Content", "Courses", f"nes_t{tid}.json"), "w") as f:
            json.dump(course, f, indent=1)

        md.append(f"## Track {tid}\n")
        md.append(f"* Laps: {data[0]}; lap length: {total_cols} cols (main race) = {total_cols * METERS_PER_COLUMN:.1f} m, "
                  f"{q_cols} cols (Challenge/qualifier layout) = {q_cols * METERS_PER_COLUMN:.1f} m.")
        counts = {}
        for s in segs:
            counts[PIECES[s['pid']]['type']] = counts.get(PIECES[s['pid']]['type'], 0) + 1
        md.append("* Pieces: " + ", ".join(f"{k} ×{v}" for k, v in sorted(counts.items())))
        md.append("")
        md.append("| ID | NES col | Gap before | NES piece | Letter | Type | Lanes | Width (cols) | Runs | Variant | Modern start (m) |")
        md.append("|---|---|---|---|---|---|---|---|---|---|---|")
        prev_end = 0
        for s in segs:
            info = PIECES[s["pid"]]
            gap = s["col"] - prev_end
            prev_end = s["col"] + s["widthCols"]
            md.append(f"| {s['id']} | {s['col']} | {gap} | ${s['pid']:02X} | {info['letter']} | {info['type']} | "
                      f"{','.join(map(str, info['lanes']))} | {s['widthCols']} | {s['runs'] or ''} | "
                      f"{'main only' if s['mainOnly'] else 'both'} | {s['col'] * METERS_PER_COLUMN:.2f} |")
        md.append("")
        if combos:
            md.append("Important combinations:\n")
            for c in combos:
                md.append(f"* **{c['id']}** ({', '.join(c['segments'])}): {c['label']}")
            md.append("")
        md.append("Additions (modern, not from the NES): start grid (two rows of four) and a run-out after the final finish line. "
                  "No turns or transition sections were added. The course is laid out straight for the full race distance, so every lap scrolls the same way.\n")
    with open(os.path.join(ROOT, "docs", "TRACK_MANIFEST.md"), "w") as f:
        f.write("\n".join(md) + "\n")
    print("wrote Content/Courses/nes_t1..5.json and docs/TRACK_MANIFEST.md")


if __name__ == "__main__":
    main()
