#!/usr/bin/env python3
"""Offline prototype of the Heatline MX bike physics (mirror of FMXBikeSim/FMXTrackModel maths)
used to pre-tune jump lengths against the NES ramp spacing before running the engine.

It rides each course in one lane with a simple strategy and reports jumps, landing grades,
face landings (would-be crashes) and lap time. The C++ implementation is the source of truth;
keep constants in sync with UMXBikeTuning defaults.
"""
import json, math, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MPC, MPR = 1.25, 0.6

T = dict(MaxSpeedNormal=27.2, MaxSpeedTurbo=30.5, AccelNormal=11.0, AccelTurbo=26.0, CoastDecel=9.0,
         SlopeGravity=14.0, Gravity=40.0, AirDragNeutral=4.0, AirDragNoseUp=12.0, NoseUpGravityScale=0.5,
         NoseDownVerticalTimeScale=0.75, DiveThrust=0.8, LaunchClearance=0.015, LaunchMinSpeed=4.0,
         PerfectTolerance=7.0, MaxLaunchAngle=36.0, CleanMin=-15.0, CleanMax=20.0, WobbleMin=-35.0, WobbleMax=50.0,
         UpslopeNoseDownCrash=-25.0, UpslopeThreshold=15.0, DownslopeNoseUpWobble=55.0, DownslopeThreshold=12.0,
         CleanSpeedLoss=0.04, WobbleSpeedLoss=0.18, FlowBoostPerfect=0.08, FlowBoostClean=0.04,
         HardLandingNormalSpeed=16.0, HardLandingLossPerMS=0.02, AbsoluteSpeedCapFraction=1.15,
         MudSpeedCap=0.55, MudDecel=38.0, GrassSpeedCap=0.5, GrassDecel=26.0, AirPitchRate=150.0)

PROFILES = {
    "RampSmall": [(0, 0), (1.5, 2), (3, 0)], "RampMedium": [(0, 0), (2.5, 3), (5, 0)],
    "RampLarge": [(0, 0), (4.5, 5), (9, 0)], "TableLow": [(0, 0), (2, 2), (7, 2), (9, 0)],
    "RampSteep": [(0, 0), (2.5, 5), (5, 0)], "RampSteepBack": [(0, 0), (3.8, 4), (6, 0)],
    "RampSteepFace": [(0, 0), (1.8, 4), (6, 0)], "Kicker": [(0, 0), (2, 2.2), (2, 0)],
    "FinishDeck": [(0, 0), (2.5, 3), (9.5, 3), (12, 0)],
}


def profile_for(seg, lane):
    t = seg["type"]
    L = seg["lengthM"]
    if t in PROFILES:
        pts = PROFILES[t]
    elif t == "Mountain":
        a, b = (seg.get("runs") or [6, 6])[:2]
        d0 = 2 + a + 2 + b
        pts = [(0, 0), (1.5, 2), (2 + a, 2), (2 + a + 1.6, 6), (d0 + 2.5, 6), (d0 + 6.5, 2), (d0 + 8.5, 2), (d0 + 10, 0)]
    elif t == "PlatformDeck":
        r1, r2, r3 = (seg.get("runs") or [3, 4, 4])[:3]
        tot = 8 + r1 + 3 + r2 + 3 + r3 + 2
        end = tot - 1
        pts = [(0, 0), (3, 6), (end, 6), (end, 0), (tot, 0)] if lane < 2 else [(0, 0), (3, 6), (5, 6), (8, 0), (tot, 0)]
    else:
        return None
    k = L / (pts[-1][0] * MPC)
    return [(c * MPC * k, r * MPR) for c, r in pts]


class Track:
    def __init__(self, course, variant="main", laps=1):
        segs = sorted(course["segments"], key=lambda s: s["startM"])
        removed = 0.0
        self.pieces = []
        for s in segs:
            if s["variant"] == "main" and variant == "challenge":
                removed += s["lengthM"]
                continue
            self.pieces.append(dict(s, s0=s["startM"] - removed, s1=s["startM"] - removed + s["lengthM"]))
        self.lap = course["lapLengthM"] - removed
        fin = [p for p in self.pieces if p["type"] == "FinishDeck"][0]
        self.finish = fin["s0"] + fin["lengthM"] / 2

    def _eval(self, p, lane, x):
        prof = profile_for(p, lane)
        if not prof:
            return 0.0, 0.0
        for (ax, ah), (bx, bh) in zip(prof, prof[1:]):
            if bx <= ax:
                continue
            if ax <= x < bx:
                return ah + (bh - ah) * (x - ax) / (bx - ax), math.degrees(math.atan2(bh - ah, bx - ax))
        return 0.0, 0.0

    def height(self, s, lane):
        h, slope = 0.0, 0.0
        for p in self.pieces:
            if p["s0"] <= s < p["s1"]:
                hh, ss = self._eval(p, lane, s - p["s0"])
                if hh >= h:
                    h, slope = hh, ss
        return h, slope

    def surface(self, s, lane):
        for p in self.pieces:
            if p["s0"] <= s < p["s1"] and p["type"] in ("Mud", "Grass", "CoolStrip") and (lane + 1) in p["lanes"]:
                return p["type"]
        return "Dirt"


def classify(delta, surf):
    cmin, cmax = T["WobbleMin"], T["WobbleMax"]
    if surf > T["UpslopeThreshold"]:
        cmin = max(cmin, T["UpslopeNoseDownCrash"])
    if surf < -T["DownslopeThreshold"]:
        cmax = max(cmax, T["DownslopeNoseUpWobble"])
    if delta < cmin or delta > cmax:
        return "crash"
    if abs(delta) <= T["PerfectTolerance"]:
        return "perfect"
    if T["CleanMin"] <= delta <= T["CleanMax"]:
        return "clean"
    return "wobble"


def ride(track, lane, turbo=True, air_input=0.0, align=True, laps_end=None, dt=1 / 120):
    """Rides to the finish; in the air the rider rotates toward the landing slope (perfect aligner) if align."""
    s, z, v = -2.0, 0.0, 0.0
    air, vx, vz, pitch, eff = False, 0.0, 0.0, 0.0, 0.0
    t = 0.0
    stats = dict(jumps=0, perfect=0, clean=0, wobble=0, crash=0, faces=0, air_time=0.0, max_air=0.0, max_len=0.0)
    launch_s = 0.0
    goal = laps_end or track.finish
    while s < goal and t < 400:
        t += dt
        if not air:
            h, slope = track.height(s, lane)
            surf = track.surface(s, lane)
            cap = T["MaxSpeedTurbo"] if turbo else T["MaxSpeedNormal"]
            acc = T["AccelTurbo"] if turbo else T["AccelNormal"]
            if v < cap:
                v = min(cap, v + acc * dt)
            if surf == "Mud" and v > T["MudSpeedCap"] * T["MaxSpeedTurbo"]:
                v = max(T["MudSpeedCap"] * T["MaxSpeedTurbo"], v - T["MudDecel"] * dt)
            if surf == "Grass" and v > T["GrassSpeedCap"] * T["MaxSpeedTurbo"]:
                v = max(T["GrassSpeedCap"] * T["MaxSpeedTurbo"], v - T["GrassDecel"] * dt)
            v -= T["SlopeGravity"] * math.sin(math.radians(slope)) * dt
            v = max(0.0, min(v, T["MaxSpeedTurbo"] * T["AbsoluteSpeedCapFraction"]))
            vxg, vzg = v * math.cos(math.radians(slope)), v * math.sin(math.radians(slope))
            s1 = s + vxg * dt
            h1, _ = track.height(s1, lane)
            zb = z + vzg * dt - 0.5 * T["Gravity"] * dt * dt
            if zb > h1 + T["LaunchClearance"] and v > T["LaunchMinSpeed"]:
                vz0 = vzg - T["Gravity"] * dt
                if math.degrees(math.atan2(vz0, vxg)) > T["MaxLaunchAngle"]:
                    # same launch-angle cap as FMXBikeSim (speed kept)
                    mag, cap = math.hypot(vxg, vz0), math.radians(T["MaxLaunchAngle"])
                    vxg, vz0 = mag * math.cos(cap), mag * math.sin(cap)
                air, vx, vz, pitch, eff = True, vxg, vz0, slope, 0.0
                s, z = s1, zb
                launch_s, air_t = s, 0.0
                stats["jumps"] += 1
            else:
                s, z = s1, h1
        else:
            air_t += dt
            eff += (air_input - eff) * (1 - math.exp(-dt / 0.1))
            up, down = max(0, eff), max(0, -eff)
            g = T["Gravity"] * (1 + (T["NoseUpGravityScale"] - 1) * up)
            drag = T["AirDragNeutral"] * max(0, 1 - up - down) + T["AirDragNoseUp"] * up
            vtime = 1 + (T["NoseDownVerticalTimeScale"] - 1) * down
            vx = max(1.5, vx + (T["DiveThrust"] * down - drag) * dt)
            vz -= g * dt * vtime
            z += vz * dt * vtime
            s += vx * dt
            h, slope = track.height(s, lane)
            if align:
                # rotate toward the surface under the predicted landing (ideal rider)
                target = slope if z - h < 1.5 else pitch
                step = T["AirPitchRate"] * dt
                pitch += max(-step, min(step, target - pitch))
            if z <= h:
                if slope > 15:
                    stats["faces"] += 1
                delta = pitch - slope
                grade = classify(delta, slope)
                stats[grade] += 1
                stats["air_time"] += air_t
                stats["max_air"] = max(stats["max_air"], air_t)
                stats["max_len"] = max(stats["max_len"], s - launch_s)
                th = math.radians(slope)
                vt = vx * math.cos(th) + vz * math.sin(th)
                vn = vx * math.sin(th) - vz * math.cos(th)
                v = max(0.0, vt)
                if vn > T["HardLandingNormalSpeed"]:
                    v *= max(0.5, 1 - (vn - T["HardLandingNormalSpeed"]) * T["HardLandingLossPerMS"])
                if grade == "perfect" and slope < -T["DownslopeThreshold"]:
                    v *= 1 + T["FlowBoostPerfect"]
                elif grade == "clean":
                    v *= 1 - T["CleanSpeedLoss"]
                elif grade == "wobble":
                    v *= 1 - T["WobbleSpeedLoss"]
                elif grade == "crash":
                    v = 0.0  # crash: restart from standstill (recovery time not modelled here)
                air, z = False, h
    stats["time"] = t
    return stats


def main():
    args = sys.argv[1:]
    for t in range(1, 6):
        course = json.load(open(os.path.join(ROOT, "Content", "Courses", f"nes_t{t}.json")))
        tr = Track(course)
        print(f"Course {t}: lap {tr.lap:.1f} m")
        for label, turbo, ai in [("turbo, neutral air", True, 0.0), ("turbo, nose-down air", True, -1.0), ("normal, neutral air", False, 0.0)]:
            st = ride(tr, 1, turbo=turbo, air_input=ai)
            print(f"   {label:22s} lap {st['time']:6.1f}s  jumps {st['jumps']:3d}  perfect {st['perfect']:3d} clean {st['clean']:3d} "
                  f"wobble {st['wobble']:3d} crash {st['crash']:3d}  faces {st['faces']:3d}  air {st['air_time']:5.1f}s  longest {st['max_len']:5.1f} m / {st['max_air']:.2f}s")


if __name__ == "__main__":
    main()
